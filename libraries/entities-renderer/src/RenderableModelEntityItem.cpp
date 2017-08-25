//
//  RenderableModelEntityItem.cpp
//  interface/src
//
//  Created by Brad Hefta-Gaub on 8/6/14.
//  Copyright 2014 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include "RenderableModelEntityItem.h"

#include <set>

#include <glm/gtx/quaternion.hpp>
#include <glm/gtx/transform.hpp>

#include <QtCore/QJsonDocument>
#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtCore/QThread>
#include <QtCore/QUrlQuery>

#include <AbstractViewStateInterface.h>
#include <CollisionRenderMeshCache.h>
#include <Model.h>
#include <PerfStat.h>
#include <render/Scene.h>
#include <DependencyManager.h>
#include <AnimationCache.h>
#include <shared/QtHelpers.h>

#include "EntityTreeRenderer.h"
#include "EntitiesRendererLogging.h"

static CollisionRenderMeshCache collisionMeshCache;

EntityItemPointer RenderableModelEntityItem::factory(const EntityItemID& entityID, const EntityItemProperties& properties) {
    EntityItemPointer entity{ new RenderableModelEntityItem(entityID, properties.getDimensionsInitialized()) };
    entity->setProperties(properties);
    return entity;
}

RenderableModelEntityItem::RenderableModelEntityItem(const EntityItemID& entityItemID, bool dimensionsInitialized) :
    ModelEntityItem(entityItemID),
    _dimensionsInitialized(dimensionsInitialized) {
}

RenderableModelEntityItem::~RenderableModelEntityItem() { }

void RenderableModelEntityItem::setDimensions(const glm::vec3& value) {
    _dimensionsInitialized = true;
    ModelEntityItem::setDimensions(value);
}

QVariantMap parseTexturesToMap(QString textures, const QVariantMap& defaultTextures) {
    // If textures are unset, revert to original textures
    if (textures.isEmpty()) {
        return defaultTextures;
    }

    // Legacy: a ,\n-delimited list of filename:"texturepath"
    if (*textures.cbegin() != '{') {
        textures = "{\"" + textures.replace(":\"", "\":\"").replace(",\n", ",\"") + "}";
    }

    QJsonParseError error;
    QJsonDocument texturesJson = QJsonDocument::fromJson(textures.toUtf8(), &error);
    // If textures are invalid, revert to original textures
    if (error.error != QJsonParseError::NoError) {
        qCWarning(entitiesrenderer) << "Could not evaluate textures property value:" << textures;
        return defaultTextures;
    }

    QVariantMap texturesMap = texturesJson.toVariant().toMap();
    // If textures are unset, revert to original textures
    if (texturesMap.isEmpty()) {
        return defaultTextures;
    }

    return texturesJson.toVariant().toMap();
}

void RenderableModelEntityItem::doInitialModelSimulation() {
    // The machinery for updateModelBounds will give existing models the opportunity to fix their
    // translation/rotation/scale/registration.  The first two are straightforward, but the latter two have guards to
    // make sure they don't happen after they've already been set.  Here we reset those guards. This doesn't cause the
    // entity values to change -- it just allows the model to match once it comes in.
    _model->setScaleToFit(false, getDimensions());
    _model->setSnapModelToRegistrationPoint(false, getRegistrationPoint());

    // now recalculate the bounds and registration
    _model->setScaleToFit(true, getDimensions());
    _model->setSnapModelToRegistrationPoint(true, getRegistrationPoint());
    _model->setRotation(getRotation());
    _model->setTranslation(getPosition());
    {
        PerformanceTimer perfTimer("_model->simulate");
        _model->simulate(0.0f);
    }
    _needsInitialSimulation = false;
}

void RenderableModelEntityItem::autoResizeJointArrays() {
    if (_model && _model->isLoaded() && !_needsInitialSimulation) {
        resizeJointArrays(_model->getJointStateCount());
    }
}

bool RenderableModelEntityItem::needsUpdateModelBounds() const {
    if (!hasModel() || !_model) {
        return false;
    }

    if (!_dimensionsInitialized || !_model->isActive()) {
        return false;
    }

    if (_model->needsReload()) {
        return true;
    }

    if (isMovingRelativeToParent() || isAnimatingSomething()) {
        return true;
    }

    if (_needsInitialSimulation || _needsJointSimulation) {
        return true;
    }

    if (_model->getScaleToFitDimensions() != getDimensions()) {
        return true;
    }

    if (_model->getRegistrationPoint() != getRegistrationPoint()) {
        return true;
    }

    bool success;
    auto transform = getTransform(success);
    if (success) {
        if (_model->getTranslation() != transform.getTranslation()) {
            return true;
        }
        if (_model->getRotation() != transform.getRotation()) {
            return true;
        }
    }

    return false;
}

void RenderableModelEntityItem::updateModelBounds() {
    if (needsUpdateModelBounds()) {
        doInitialModelSimulation();
        _needsJointSimulation = false;
    }
}

void RenderableModelEntityItem::setModel(const ModelPointer& model) {
    withWriteLock([&] {
        if (_model != model) {
            _model = model;
            if (_model) {
                _needsInitialSimulation = true;
            }
        }
    });
}

EntityItemProperties RenderableModelEntityItem::getProperties(EntityPropertyFlags desiredProperties) const {
    EntityItemProperties properties = ModelEntityItem::getProperties(desiredProperties); // get the properties from our base class
    if (_originalTexturesRead) {
        properties.setTextureNames(_originalTextures);
    }

    if (_model) {
        properties.setRenderInfoVertexCount(_model->getRenderInfoVertexCount());
        properties.setRenderInfoTextureCount(_model->getRenderInfoTextureCount());
        properties.setRenderInfoTextureSize(_model->getRenderInfoTextureSize());
        properties.setRenderInfoDrawCalls(_model->getRenderInfoDrawCalls());
        properties.setRenderInfoHasTransparent(_model->getRenderInfoHasTransparent());
    }


    if (_model && _model->isLoaded()) {
        // TODO: improve naturalDimensions in the future,
        //       for now we've added this hack for setting natural dimensions of models
        Extents meshExtents = _model->getFBXGeometry().getUnscaledMeshExtents();
        properties.setNaturalDimensions(meshExtents.maximum - meshExtents.minimum);
        properties.calculateNaturalPosition(meshExtents.minimum, meshExtents.maximum);
    }

    return properties;
}

bool RenderableModelEntityItem::supportsDetailedRayIntersection() const {
    return resultWithReadLock<bool>([&] {
        return _model && _model->isLoaded();
    });
}

bool RenderableModelEntityItem::findDetailedRayIntersection(const glm::vec3& origin, const glm::vec3& direction,
                         bool& keepSearching, OctreeElementPointer& element, float& distance, BoxFace& face,
                         glm::vec3& surfaceNormal, void** intersectedObject, bool precisionPicking) const {
    if (!_model) {
        return true;
    }
    // qCDebug(entitiesrenderer) << "RenderableModelEntityItem::findDetailedRayIntersection() precisionPicking:"
    //                           << precisionPicking;

    QString extraInfo;
    return _model->findRayIntersectionAgainstSubMeshes(origin, direction, distance,
                                                       face, surfaceNormal, extraInfo, precisionPicking, false);
}

void RenderableModelEntityItem::getCollisionGeometryResource() {
    QUrl hullURL(getCompoundShapeURL());
    QUrlQuery queryArgs(hullURL);
    queryArgs.addQueryItem("collision-hull", "");
    hullURL.setQuery(queryArgs);
    _compoundShapeResource = DependencyManager::get<ModelCache>()->getCollisionGeometryResource(hullURL);
}

void RenderableModelEntityItem::setShapeType(ShapeType type) {
    ModelEntityItem::setShapeType(type);
    if (getShapeType() == SHAPE_TYPE_COMPOUND) {
        if (!_compoundShapeResource && !getCompoundShapeURL().isEmpty()) {
            getCollisionGeometryResource();
        }
    } else if (_compoundShapeResource && !getCompoundShapeURL().isEmpty()) {
        // the compoundURL has been set but the shapeType does not agree
        _compoundShapeResource.reset();
    }
}

void RenderableModelEntityItem::setCompoundShapeURL(const QString& url) {
    // because the caching system only allows one Geometry per url, and because this url might also be used
    // as a visual model, we need to change this url in some way.  We add a "collision-hull" query-arg so it
    // will end up in a different hash-key in ResourceCache.  TODO: It would be better to use the same URL and
    // parse it twice.
    auto currentCompoundShapeURL = getCompoundShapeURL();
    ModelEntityItem::setCompoundShapeURL(url);
    if (getCompoundShapeURL() != currentCompoundShapeURL || !_model) {
        if (getShapeType() == SHAPE_TYPE_COMPOUND) {
            getCollisionGeometryResource();
        }
    }
}

bool RenderableModelEntityItem::isReadyToComputeShape() const {
    ShapeType type = getShapeType();

    if (type == SHAPE_TYPE_COMPOUND) {
        if (!_model || getCompoundShapeURL().isEmpty()) {
            return false;
        }

        if (_model->getURL().isEmpty()) {
            // we need a render geometry with a scale to proceed, so give up.
            return false;
        }

        if (_model->isLoaded()) {
            if (!getCompoundShapeURL().isEmpty() && !_compoundShapeResource) {
                const_cast<RenderableModelEntityItem*>(this)->getCollisionGeometryResource();
            }

            if (_compoundShapeResource && _compoundShapeResource->isLoaded()) {
                // we have both URLs AND both geometries AND they are both fully loaded.
                if (_needsInitialSimulation) {
                    // the _model's offset will be wrong until _needsInitialSimulation is false
                    PerformanceTimer perfTimer("_model->simulate");
                    const_cast<RenderableModelEntityItem*>(this)->doInitialModelSimulation();
                }
                return true;
            }
        }

        // the model is still being downloaded.
        return false;
    } else if (type >= SHAPE_TYPE_SIMPLE_HULL && type <= SHAPE_TYPE_STATIC_MESH) {
        return (_model && _model->isLoaded());
    }
    return true;
}

void RenderableModelEntityItem::computeShapeInfo(ShapeInfo& shapeInfo) {
    const uint32_t TRIANGLE_STRIDE = 3;
    const uint32_t QUAD_STRIDE = 4;

    ShapeType type = getShapeType();
    glm::vec3 dimensions = getDimensions();
    if (type == SHAPE_TYPE_COMPOUND) {
        updateModelBounds();

        // should never fall in here when collision model not fully loaded
        // hence we assert that all geometries exist and are loaded
        assert(_model && _model->isLoaded() && _compoundShapeResource && _compoundShapeResource->isLoaded());
        const FBXGeometry& collisionGeometry = _compoundShapeResource->getFBXGeometry();

        ShapeInfo::PointCollection& pointCollection = shapeInfo.getPointCollection();
        pointCollection.clear();
        uint32_t i = 0;

        // the way OBJ files get read, each section under a "g" line is its own meshPart.  We only expect
        // to find one actual "mesh" (with one or more meshParts in it), but we loop over the meshes, just in case.
        foreach (const FBXMesh& mesh, collisionGeometry.meshes) {
            // each meshPart is a convex hull
            foreach (const FBXMeshPart &meshPart, mesh.parts) {
                pointCollection.push_back(QVector<glm::vec3>());
                ShapeInfo::PointList& pointsInPart = pointCollection[i];

                // run through all the triangles and (uniquely) add each point to the hull
                uint32_t numIndices = (uint32_t)meshPart.triangleIndices.size();
                // TODO: assert rather than workaround after we start sanitizing FBXMesh higher up
                //assert(numIndices % TRIANGLE_STRIDE == 0);
                numIndices -= numIndices % TRIANGLE_STRIDE; // WORKAROUND lack of sanity checking in FBXReader

                for (uint32_t j = 0; j < numIndices; j += TRIANGLE_STRIDE) {
                    glm::vec3 p0 = mesh.vertices[meshPart.triangleIndices[j]];
                    glm::vec3 p1 = mesh.vertices[meshPart.triangleIndices[j + 1]];
                    glm::vec3 p2 = mesh.vertices[meshPart.triangleIndices[j + 2]];
                    if (!pointsInPart.contains(p0)) {
                        pointsInPart << p0;
                    }
                    if (!pointsInPart.contains(p1)) {
                        pointsInPart << p1;
                    }
                    if (!pointsInPart.contains(p2)) {
                        pointsInPart << p2;
                    }
                }

                // run through all the quads and (uniquely) add each point to the hull
                numIndices = (uint32_t)meshPart.quadIndices.size();
                // TODO: assert rather than workaround after we start sanitizing FBXMesh higher up
                //assert(numIndices % QUAD_STRIDE == 0);
                numIndices -= numIndices % QUAD_STRIDE; // WORKAROUND lack of sanity checking in FBXReader

                for (uint32_t j = 0; j < numIndices; j += QUAD_STRIDE) {
                    glm::vec3 p0 = mesh.vertices[meshPart.quadIndices[j]];
                    glm::vec3 p1 = mesh.vertices[meshPart.quadIndices[j + 1]];
                    glm::vec3 p2 = mesh.vertices[meshPart.quadIndices[j + 2]];
                    glm::vec3 p3 = mesh.vertices[meshPart.quadIndices[j + 3]];
                    if (!pointsInPart.contains(p0)) {
                        pointsInPart << p0;
                    }
                    if (!pointsInPart.contains(p1)) {
                        pointsInPart << p1;
                    }
                    if (!pointsInPart.contains(p2)) {
                        pointsInPart << p2;
                    }
                    if (!pointsInPart.contains(p3)) {
                        pointsInPart << p3;
                    }
                }

                if (pointsInPart.size() == 0) {
                    qCDebug(entitiesrenderer) << "Warning -- meshPart has no faces";
                    pointCollection.pop_back();
                    continue;
                }
                ++i;
            }
        }

        // We expect that the collision model will have the same units and will be displaced
        // from its origin in the same way the visual model is.  The visual model has
        // been centered and probably scaled.  We take the scaling and offset which were applied
        // to the visual model and apply them to the collision model (without regard for the
        // collision model's extents).

        glm::vec3 scaleToFit = dimensions / _model->getFBXGeometry().getUnscaledMeshExtents().size();
        // multiply each point by scale before handing the point-set off to the physics engine.
        // also determine the extents of the collision model.
        glm::vec3 registrationOffset = dimensions * (ENTITY_ITEM_DEFAULT_REGISTRATION_POINT - getRegistrationPoint());
        for (int32_t i = 0; i < pointCollection.size(); i++) {
            for (int32_t j = 0; j < pointCollection[i].size(); j++) {
                // back compensate for registration so we can apply that offset to the shapeInfo later
                pointCollection[i][j] = scaleToFit * (pointCollection[i][j] + _model->getOffset()) - registrationOffset;
            }
        }
        shapeInfo.setParams(type, dimensions, getCompoundShapeURL());
    } else if (type >= SHAPE_TYPE_SIMPLE_HULL && type <= SHAPE_TYPE_STATIC_MESH) {
        // should never fall in here when model not fully loaded
        assert(_model && _model->isLoaded());

        updateModelBounds();
        _model->updateGeometry();

        // compute meshPart local transforms
        QVector<glm::mat4> localTransforms;
        const FBXGeometry& fbxGeometry = _model->getFBXGeometry();
        int numFbxMeshes = fbxGeometry.meshes.size();
        int totalNumVertices = 0;
        glm::mat4 invRegistraionOffset = glm::translate(dimensions * (getRegistrationPoint() - ENTITY_ITEM_DEFAULT_REGISTRATION_POINT));
        for (int i = 0; i < numFbxMeshes; i++) {
            const FBXMesh& mesh = fbxGeometry.meshes.at(i);
            if (mesh.clusters.size() > 0) {
                const FBXCluster& cluster = mesh.clusters.at(0);
                auto jointMatrix = _model->getRig().getJointTransform(cluster.jointIndex);
                // we backtranslate by the registration offset so we can apply that offset to the shapeInfo later
                localTransforms.push_back(invRegistraionOffset * jointMatrix * cluster.inverseBindMatrix);
            } else {
                glm::mat4 identity;
                localTransforms.push_back(invRegistraionOffset);
            }
            totalNumVertices += mesh.vertices.size();
        }
        const int32_t MAX_VERTICES_PER_STATIC_MESH = 1e6;
        if (totalNumVertices > MAX_VERTICES_PER_STATIC_MESH) {
            qWarning() << "model" << getModelURL() << "has too many vertices" << totalNumVertices << "and will collide as a box.";
            shapeInfo.setParams(SHAPE_TYPE_BOX, 0.5f * dimensions);
            return;
        }

        auto& meshes = _model->getGeometry()->getMeshes();
        int32_t numMeshes = (int32_t)(meshes.size());

        const int MAX_ALLOWED_MESH_COUNT = 1000;
        if (numMeshes > MAX_ALLOWED_MESH_COUNT) {
            // too many will cause the deadlock timer to throw...
            shapeInfo.setParams(SHAPE_TYPE_BOX, 0.5f * dimensions);
            return;
        }

        ShapeInfo::PointCollection& pointCollection = shapeInfo.getPointCollection();
        pointCollection.clear();
        if (type == SHAPE_TYPE_SIMPLE_COMPOUND) {
            pointCollection.resize(numMeshes);
        } else {
            pointCollection.resize(1);
        }

        ShapeInfo::TriangleIndices& triangleIndices = shapeInfo.getTriangleIndices();
        triangleIndices.clear();

        Extents extents;
        int32_t meshCount = 0;
        int32_t pointListIndex = 0;
        for (auto& mesh : meshes) {
            if (!mesh) {
                continue;
            }
            const gpu::BufferView& vertices = mesh->getVertexBuffer();
            const gpu::BufferView& indices = mesh->getIndexBuffer();
            const gpu::BufferView& parts = mesh->getPartBuffer();

            ShapeInfo::PointList& points = pointCollection[pointListIndex];

            // reserve room
            int32_t sizeToReserve = (int32_t)(vertices.getNumElements());
            if (type == SHAPE_TYPE_SIMPLE_COMPOUND) {
                // a list of points for each mesh
                pointListIndex++;
            } else {
                // only one list of points
                sizeToReserve += (int32_t)((gpu::Size)points.size());
            }
            points.reserve(sizeToReserve);

            // copy points
            uint32_t meshIndexOffset = (uint32_t)points.size();
            const glm::mat4& localTransform = localTransforms[meshCount];
            gpu::BufferView::Iterator<const glm::vec3> vertexItr = vertices.cbegin<const glm::vec3>();
            while (vertexItr != vertices.cend<const glm::vec3>()) {
                glm::vec3 point = extractTranslation(localTransform * glm::translate(*vertexItr));
                points.push_back(point);
                extents.addPoint(point);
                ++vertexItr;
            }

            if (type == SHAPE_TYPE_STATIC_MESH) {
                // copy into triangleIndices
                triangleIndices.reserve((int32_t)((gpu::Size)(triangleIndices.size()) + indices.getNumElements()));
                gpu::BufferView::Iterator<const model::Mesh::Part> partItr = parts.cbegin<const model::Mesh::Part>();
                while (partItr != parts.cend<const model::Mesh::Part>()) {
                    auto numIndices = partItr->_numIndices;
                    if (partItr->_topology == model::Mesh::TRIANGLES) {
                        // TODO: assert rather than workaround after we start sanitizing FBXMesh higher up
                        //assert(numIndices % TRIANGLE_STRIDE == 0);
                        numIndices -= numIndices % TRIANGLE_STRIDE; // WORKAROUND lack of sanity checking in FBXReader

                        auto indexItr = indices.cbegin<const gpu::BufferView::Index>() + partItr->_startIndex;
                        auto indexEnd = indexItr + numIndices;
                        while (indexItr != indexEnd) {
                            triangleIndices.push_back(*indexItr + meshIndexOffset);
                            ++indexItr;
                        }
                    } else if (partItr->_topology == model::Mesh::TRIANGLE_STRIP) {
                        // TODO: resurrect assert after we start sanitizing FBXMesh higher up
                        //assert(numIndices > 2);

                        uint32_t approxNumIndices = TRIANGLE_STRIDE * numIndices;
                        if (approxNumIndices > (uint32_t)(triangleIndices.capacity() - triangleIndices.size())) {
                            // we underestimated the final size of triangleIndices so we pre-emptively expand it
                            triangleIndices.reserve(triangleIndices.size() + approxNumIndices);
                        }

                        auto indexItr = indices.cbegin<const gpu::BufferView::Index>() + partItr->_startIndex;
                        auto indexEnd = indexItr + (numIndices - 2);

                        // first triangle uses the first three indices
                        triangleIndices.push_back(*(indexItr++) + meshIndexOffset);
                        triangleIndices.push_back(*(indexItr++) + meshIndexOffset);
                        triangleIndices.push_back(*(indexItr++) + meshIndexOffset);

                        // the rest use previous and next index
                        uint32_t triangleCount = 1;
                        while (indexItr != indexEnd) {
                            if ((*indexItr) != model::Mesh::PRIMITIVE_RESTART_INDEX) {
                                if (triangleCount % 2 == 0) {
                                    // even triangles use first two indices in order
                                    triangleIndices.push_back(*(indexItr - 2) + meshIndexOffset);
                                    triangleIndices.push_back(*(indexItr - 1) + meshIndexOffset);
                                } else {
                                    // odd triangles swap order of first two indices
                                    triangleIndices.push_back(*(indexItr - 1) + meshIndexOffset);
                                    triangleIndices.push_back(*(indexItr - 2) + meshIndexOffset);
                                }
                                triangleIndices.push_back(*indexItr + meshIndexOffset);
                                ++triangleCount;
                            }
                            ++indexItr;
                        }
                    }
                    ++partItr;
                }
            } else if (type == SHAPE_TYPE_SIMPLE_COMPOUND) {
                // for each mesh copy unique part indices, separated by special bogus (flag) index values
                gpu::BufferView::Iterator<const model::Mesh::Part> partItr = parts.cbegin<const model::Mesh::Part>();
                while (partItr != parts.cend<const model::Mesh::Part>()) {
                    // collect unique list of indices for this part
                    std::set<int32_t> uniqueIndices;
                    auto numIndices = partItr->_numIndices;
                    if (partItr->_topology == model::Mesh::TRIANGLES) {
                        // TODO: assert rather than workaround after we start sanitizing FBXMesh higher up
                        //assert(numIndices% TRIANGLE_STRIDE == 0);
                        numIndices -= numIndices % TRIANGLE_STRIDE; // WORKAROUND lack of sanity checking in FBXReader

                        auto indexItr = indices.cbegin<const gpu::BufferView::Index>() + partItr->_startIndex;
                        auto indexEnd = indexItr + numIndices;
                        while (indexItr != indexEnd) {
                            uniqueIndices.insert(*indexItr);
                            ++indexItr;
                        }
                    } else if (partItr->_topology == model::Mesh::TRIANGLE_STRIP) {
                        // TODO: resurrect assert after we start sanitizing FBXMesh higher up
                        //assert(numIndices > TRIANGLE_STRIDE - 1);

                        auto indexItr = indices.cbegin<const gpu::BufferView::Index>() + partItr->_startIndex;
                        auto indexEnd = indexItr + (numIndices - 2);

                        // first triangle uses the first three indices
                        uniqueIndices.insert(*(indexItr++));
                        uniqueIndices.insert(*(indexItr++));
                        uniqueIndices.insert(*(indexItr++));

                        // the rest use previous and next index
                        uint32_t triangleCount = 1;
                        while (indexItr != indexEnd) {
                            if ((*indexItr) != model::Mesh::PRIMITIVE_RESTART_INDEX) {
                                if (triangleCount % 2 == 0) {
                                    // EVEN triangles use first two indices in order
                                    uniqueIndices.insert(*(indexItr - 2));
                                    uniqueIndices.insert(*(indexItr - 1));
                                } else {
                                    // ODD triangles swap order of first two indices
                                    uniqueIndices.insert(*(indexItr - 1));
                                    uniqueIndices.insert(*(indexItr - 2));
                                }
                                uniqueIndices.insert(*indexItr);
                                ++triangleCount;
                            }
                            ++indexItr;
                        }
                    }

                    // store uniqueIndices in triangleIndices
                    triangleIndices.reserve(triangleIndices.size() + (int32_t)uniqueIndices.size());
                    for (auto index : uniqueIndices) {
                        triangleIndices.push_back(index);
                    }
                    // flag end of part
                    triangleIndices.push_back(END_OF_MESH_PART);

                    ++partItr;
                }
                // flag end of mesh
                triangleIndices.push_back(END_OF_MESH);
            }
            ++meshCount;
        }

        // scale and shift
        glm::vec3 extentsSize = extents.size();
        glm::vec3 scaleToFit = dimensions / extentsSize;
        for (int32_t i = 0; i < 3; ++i) {
            if (extentsSize[i] < 1.0e-6f) {
                scaleToFit[i] = 1.0f;
            }
        }
        for (auto points : pointCollection) {
            for (int32_t i = 0; i < points.size(); ++i) {
                points[i] = (points[i] * scaleToFit);
            }
        }

        shapeInfo.setParams(type, 0.5f * dimensions, getModelURL());
    } else {
        ModelEntityItem::computeShapeInfo(shapeInfo);
        shapeInfo.setParams(type, 0.5f * dimensions);
    }
    // finally apply the registration offset to the shapeInfo
    adjustShapeInfoByRegistration(shapeInfo);
}

void RenderableModelEntityItem::setCollisionShape(const btCollisionShape* shape) {
    const void* key = static_cast<const void*>(shape);
    if (_collisionMeshKey != key) {
        if (_collisionMeshKey) {
            collisionMeshCache.releaseMesh(_collisionMeshKey);
        }
        _collisionMeshKey = key;
        // toggle _showCollisionGeometry forces re-evaluation later
        _showCollisionGeometry = !_showCollisionGeometry;
    }
}

bool RenderableModelEntityItem::contains(const glm::vec3& point) const {
    if (EntityItem::contains(point) && _model && _compoundShapeResource && _compoundShapeResource->isLoaded()) {
        return _compoundShapeResource->getFBXGeometry().convexHullContains(worldToEntity(point));
    }

    return false;
}

bool RenderableModelEntityItem::shouldBePhysical() const {
    // If we have a model, make sure it hasn't failed to download.
    // If it has, we'll report back that we shouldn't be physical so that physics aren't held waiting for us to be ready.
    if (_model && getShapeType() == SHAPE_TYPE_COMPOUND && _model->didCollisionGeometryRequestFail()) {
        return false;
    } else if (_model && getShapeType() != SHAPE_TYPE_NONE && _model->didVisualGeometryRequestFail()) {
        return false;
    } else {
        return ModelEntityItem::shouldBePhysical();
    }
}

glm::quat RenderableModelEntityItem::getAbsoluteJointRotationInObjectFrame(int index) const {
    if (_model) {
        glm::quat result;
        if (_model->getAbsoluteJointRotationInRigFrame(index, result)) {
            return result;
        }
    }
    return glm::quat();
}

glm::vec3 RenderableModelEntityItem::getAbsoluteJointTranslationInObjectFrame(int index) const {
    if (_model) {
        glm::vec3 result;
        if (_model->getAbsoluteJointTranslationInRigFrame(index, result)) {
            return result;
        }
    }
    return glm::vec3(0.0f);
}

bool RenderableModelEntityItem::setAbsoluteJointRotationInObjectFrame(int index, const glm::quat& rotation) {
    if (!_model) {
        return false;
    }
    const Rig& rig = _model->getRig();
    int jointParentIndex = rig.getJointParentIndex(index);
    if (jointParentIndex == -1) {
        return setLocalJointRotation(index, rotation);
    }

    bool success;
    AnimPose jointParentPose;
    success = rig.getAbsoluteJointPoseInRigFrame(jointParentIndex, jointParentPose);
    if (!success) {
        return false;
    }
    AnimPose jointParentInversePose = jointParentPose.inverse();

    AnimPose jointAbsolutePose; // in rig frame
    success = rig.getAbsoluteJointPoseInRigFrame(index, jointAbsolutePose);
    if (!success) {
        return false;
    }
    jointAbsolutePose.rot() = rotation;

    AnimPose jointRelativePose = jointParentInversePose * jointAbsolutePose;
    return setLocalJointRotation(index, jointRelativePose.rot());
}

bool RenderableModelEntityItem::setAbsoluteJointTranslationInObjectFrame(int index, const glm::vec3& translation) {
    if (!_model) {
        return false;
    }
    const Rig& rig = _model->getRig();

    int jointParentIndex = rig.getJointParentIndex(index);
    if (jointParentIndex == -1) {
        return setLocalJointTranslation(index, translation);
    }

    bool success;
    AnimPose jointParentPose;
    success = rig.getAbsoluteJointPoseInRigFrame(jointParentIndex, jointParentPose);
    if (!success) {
        return false;
    }
    AnimPose jointParentInversePose = jointParentPose.inverse();

    AnimPose jointAbsolutePose; // in rig frame
    success = rig.getAbsoluteJointPoseInRigFrame(index, jointAbsolutePose);
    if (!success) {
        return false;
    }
    jointAbsolutePose.trans() = translation;

    AnimPose jointRelativePose = jointParentInversePose * jointAbsolutePose;
    return setLocalJointTranslation(index, jointRelativePose.trans());
}

glm::quat RenderableModelEntityItem::getLocalJointRotation(int index) const {
    if (_model) {
        glm::quat result;
        if (_model->getJointRotation(index, result)) {
            return result;
        }
    }
    return glm::quat();
}

glm::vec3 RenderableModelEntityItem::getLocalJointTranslation(int index) const {
    if (_model) {
        glm::vec3 result;
        if (_model->getJointTranslation(index, result)) {
            return result;
        }
    }
    return glm::vec3();
}

bool RenderableModelEntityItem::setLocalJointRotation(int index, const glm::quat& rotation) {
    autoResizeJointArrays();
    bool result = false;
    _jointDataLock.withWriteLock([&] {
        _jointRotationsExplicitlySet = true;
        if (index >= 0 && index < _localJointData.size()) {
            auto& jointData = _localJointData[index];
            if (jointData.joint.rotation != rotation) {
                jointData.joint.rotation = rotation;
                jointData.joint.rotationSet = true;
                jointData.rotationDirty = true;
                result = true;
                _needsJointSimulation = true;
            }
        }
    });
    return result;
}

bool RenderableModelEntityItem::setLocalJointTranslation(int index, const glm::vec3& translation) {
    autoResizeJointArrays();
    bool result = false;
    _jointDataLock.withWriteLock([&] {
        _jointTranslationsExplicitlySet = true;
        if (index >= 0 && index < _localJointData.size()) {
            auto& jointData = _localJointData[index];
            if (jointData.joint.translation != translation) {
                jointData.joint.translation = translation;
                jointData.joint.translationSet = true;
                jointData.translationDirty = true;
                result = true;
                _needsJointSimulation = true;
            }
        }
    });
    return result;
}

void RenderableModelEntityItem::setJointRotations(const QVector<glm::quat>& rotations) {
    ModelEntityItem::setJointRotations(rotations);
    _needsJointSimulation = true;
}

void RenderableModelEntityItem::setJointRotationsSet(const QVector<bool>& rotationsSet) {
    ModelEntityItem::setJointRotationsSet(rotationsSet);
    _needsJointSimulation = true;
}

void RenderableModelEntityItem::setJointTranslations(const QVector<glm::vec3>& translations) {
    ModelEntityItem::setJointTranslations(translations);
    _needsJointSimulation = true;
}

void RenderableModelEntityItem::setJointTranslationsSet(const QVector<bool>& translationsSet) {
    ModelEntityItem::setJointTranslationsSet(translationsSet);
    _needsJointSimulation = true;
}

void RenderableModelEntityItem::locationChanged(bool tellPhysics) {
    PerformanceTimer pertTimer("locationChanged");
    EntityItem::locationChanged(tellPhysics);
    if (_model && _model->isLoaded()) {
        _model->updateRenderItems();
    }
}

int RenderableModelEntityItem::getJointIndex(const QString& name) const {
    return (_model && _model->isActive()) ? _model->getRig().indexOfJoint(name) : -1;
}

QStringList RenderableModelEntityItem::getJointNames() const {
    QStringList result;
    if (_model && _model->isActive()) {
        const Rig& rig = _model->getRig();
        int jointCount = rig.getJointStateCount();
        for (int jointIndex = 0; jointIndex < jointCount; jointIndex++) {
            result << rig.nameOfJoint(jointIndex);
        }
    }
    return result;
}

bool RenderableModelEntityItem::getMeshes(MeshProxyList& result) {
    if (!_model || !_model->isLoaded()) {
        return false;
    }
    BLOCKING_INVOKE_METHOD(_model.get(), "getMeshes", Q_RETURN_ARG(MeshProxyList, result));
    return !result.isEmpty();
}

void RenderableModelEntityItem::copyAnimationJointDataToModel() {
    ModelPointer model; 
    withReadLock([&] {
        model = _model;
    });

    if (!model || !model->isLoaded()) {
        return;
    }


    // relay any inbound joint changes from scripts/animation/network to the model/rig
    _jointDataLock.withWriteLock([&] {
        for (int index = 0; index < _localJointData.size(); ++index) {
            auto& jointData = _localJointData[index];
            if (jointData.rotationDirty) {
                model->setJointRotation(index, true, jointData.joint.rotation, 1.0f);
                jointData.rotationDirty = false;
            }
            if (jointData.translationDirty) {
                _model->setJointTranslation(index, true, jointData.joint.translation, 1.0f);
                jointData.translationDirty = false;
            }
        }
    });
}

bool RenderableModelEntityItem::isAnimatingSomething() const {
    return !getAnimationURL().isEmpty() && getAnimationIsPlaying() && getAnimationFPS() != 0.0f;
}

using namespace render;
using namespace render::entities;

ItemKey ModelEntityRenderer::getKey() {
    return ItemKey::Builder::opaqueShape().withTypeMeta();
}

uint32_t ModelEntityRenderer::metaFetchMetaSubItems(ItemIDs& subItems) { 
    if (_model) {
        auto metaSubItems = _model->fetchRenderItemIDs();
        subItems.insert(subItems.end(), metaSubItems.begin(), metaSubItems.end());
        return (uint32_t)metaSubItems.size();
    }
    return 0;
}

void ModelEntityRenderer::removeFromScene(const ScenePointer& scene, Transaction& transaction) {
    if (_model) {
        _model->removeFromScene(scene, transaction);
    }
    Parent::removeFromScene(scene, transaction);
}

void ModelEntityRenderer::onRemoveFromSceneTyped(const TypedEntityPointer& entity) {
    entity->setModel({});
}

bool operator!=(const AnimationPropertyGroup& a, const AnimationPropertyGroup& b) {
    return !(a == b);
}

void ModelEntityRenderer::animate(const TypedEntityPointer& entity) {
    if (!_animation || !_animation->isLoaded()) {
        return;
    }

    QVector<JointData> jointsData;

    const QVector<FBXAnimationFrame>&  frames = _animation->getFramesReference(); // NOTE: getFrames() is too heavy
    auto& fbxJoints = _animation->getGeometry().joints;
    int frameCount = frames.size();
    if (frameCount <= 0) {
        return;
    }

    if (!_lastAnimated) {
        _lastAnimated = usecTimestampNow();
        return;
    }

    auto now = usecTimestampNow();
    auto interval = now - _lastAnimated;
    _lastAnimated = now;
    float deltaTime = (float)interval / (float)USECS_PER_SECOND;
    _currentFrame += (deltaTime * _renderAnimationProperties.getFPS());

    {
        int animationCurrentFrame = (int)(glm::floor(_currentFrame)) % frameCount;
        if (animationCurrentFrame < 0 || animationCurrentFrame > frameCount) {
            animationCurrentFrame = 0;
        }

        if (animationCurrentFrame == _lastKnownCurrentFrame) {
            return;
        }
        _lastKnownCurrentFrame = animationCurrentFrame;
    }

    if (_jointMapping.size() != _model->getJointStateCount()) {
        qCWarning(entitiesrenderer) << "RenderableModelEntityItem::getAnimationFrame -- joint count mismatch"
                    << _jointMapping.size() << _model->getJointStateCount();
        return;
    }

    const QVector<glm::quat>& rotations = frames[_lastKnownCurrentFrame].rotations;
    const QVector<glm::vec3>& translations = frames[_lastKnownCurrentFrame].translations;
                
    jointsData.resize(_jointMapping.size());
    for (int j = 0; j < _jointMapping.size(); j++) {
        int index = _jointMapping[j];
        if (index >= 0) {
            glm::mat4 translationMat;
            if (index < translations.size()) {
                translationMat = glm::translate(translations[index]);
            }
            glm::mat4 rotationMat;
            if (index < rotations.size()) {
                rotationMat = glm::mat4_cast(fbxJoints[index].preRotation * rotations[index] * fbxJoints[index].postRotation);
            } else {
                rotationMat = glm::mat4_cast(fbxJoints[index].preRotation * fbxJoints[index].postRotation);
            }

            glm::mat4 finalMat = (translationMat * fbxJoints[index].preTransform *
                rotationMat * fbxJoints[index].postTransform);
            auto& jointData = jointsData[j];
            jointData.translation = extractTranslation(finalMat);
            jointData.translationSet = true;
            jointData.rotation = glmExtractRotation(finalMat);
            jointData.rotationSet = true;
        }
    }

    // Set the data in the entity
    entity->setAnimationJointsData(jointsData);

    entity->copyAnimationJointDataToModel();
}

bool ModelEntityRenderer::needsRenderUpdate() const {
    ModelPointer model;
    withReadLock([&] {
        model = _model;
    });

    if (model) {
        if (_needsJointSimulation || _moving || _animating) {
            return true;
        }

        // When the individual mesh parts of a model finish fading, they will mark their Model as needing updating
        // we will watch for that and ask the model to update it's render items
        if (_parsedModelURL != model->getURL()) {
            return true;
        }

        if (model->needsReload()) {
            return true;
        }

        // FIXME what is the difference between these two?
        if (model->needsFixupInScene()) {
            return true;
        }

        // FIXME what is the difference between these two? ^^^^
        if (model->getRenderItemsNeedUpdate()) {
            return true;
        }
    }
    return Parent::needsRenderUpdate();
}

bool ModelEntityRenderer::needsRenderUpdateFromTypedEntity(const TypedEntityPointer& entity) const {
    if (resultWithReadLock<bool>([&] {
        if (entity->hasModel() != _hasModel) {
            return true;
        }

        if (_lastModelURL != entity->getModelURL()) {
            return true;
        }

        // No model to render, early exit
        if (!_hasModel) {
            return false;
        }

        if (_lastTextures != entity->getTextures()) {
            return true;
        }

        if (_renderAnimationProperties != entity->getAnimationProperties()) {
            return true;
        }

        if (_animating != entity->isAnimatingSomething()) {
            return true;
        }

        return false;
    })) { return true; }

    ModelPointer model;
    withReadLock([&] {
        model = _model;
    });

    if (model && model->isLoaded()) {
        if (!entity->_dimensionsInitialized || entity->_needsInitialSimulation) {
            return true;
        }

        // Check to see if we need to update the model bounds
        if (entity->needsUpdateModelBounds()) {
            return true;
        }

        // Check to see if we need to update the model bounds
        auto transform = entity->getTransform();
        if (model->getTranslation() != transform.getTranslation() ||
            model->getRotation() != transform.getRotation()) {
            return true;
        }


        if (model->getScaleToFitDimensions() != entity->getDimensions() ||
            model->getRegistrationPoint() != entity->getRegistrationPoint()) {
            return true;
        }
    }

    return false;
}

void ModelEntityRenderer::doRenderUpdateSynchronousTyped(const ScenePointer& scene, Transaction& transaction, const TypedEntityPointer& entity) {
    if (_hasModel != entity->hasModel()) {
        _hasModel = entity->hasModel();
    }

    _marketplaceEntity = entity->getMarketplaceID().length() != 0;
    _shouldHighlight = entity->getShouldHighlight();
    _animating = entity->isAnimatingSomething();

    withWriteLock([&] {
        if (_lastModelURL != entity->getModelURL()) {
            _lastModelURL = entity->getModelURL();
            _parsedModelURL = QUrl(_lastModelURL);
        }
    });

    // Check for removal
    ModelPointer model;
    withReadLock([&] { model = _model; });
    if (!_hasModel) {
        if ((bool)model) {
            model->removeFromScene(scene, transaction);
            withWriteLock([&] { _model.reset(); });
        }
        return;
    }

    // Check for addition
    if (_hasModel && !(bool)_model) {
        model = std::make_shared<Model>(nullptr, entity.get());
        model->setLoadingPriority(EntityTreeRenderer::getEntityLoadingPriority(*entity));
        model->init();
        entity->setModel(model);
        withWriteLock([&] { _model = model; });
    }

    // From here on, we are guaranteed a populated model
    withWriteLock([&] {
        if (_parsedModelURL != model->getURL()) {
            model->setURL(_parsedModelURL);
        }
    });

    // Nothing else to do unless the model is loaded
    if (!model->isLoaded()) {
        return;
    }

    // Check for initializing the model
    if (!entity->_dimensionsInitialized) {
        EntityItemProperties properties;
        properties.setLastEdited(usecTimestampNow()); // we must set the edit time since we're editing it
        auto extents = model->getMeshExtents();
        properties.setDimensions(extents.maximum - extents.minimum);
        qCDebug(entitiesrenderer) << "Autoresizing" 
            << (!entity->getName().isEmpty() ?  entity->getName() : entity->getModelURL()) 
            << "from mesh extents";

        QMetaObject::invokeMethod(DependencyManager::get<EntityScriptingInterface>().data(), "editEntity",
            Qt::QueuedConnection, Q_ARG(QUuid, entity->getEntityItemID()), Q_ARG(EntityItemProperties, properties));
    }

    if (!entity->_originalTexturesRead) {
        // Default to _originalTextures to avoid remapping immediately and lagging on load
        entity->_originalTextures = model->getTextures();
        entity->_originalTexturesRead = true;
        _currentTextures = entity->_originalTextures;
    }

    if (_lastTextures != entity->getTextures()) {
        _lastTextures = entity->getTextures();
        auto newTextures = parseTexturesToMap(_lastTextures, entity->_originalTextures);
        if (newTextures != _currentTextures) {
            model->setTextures(newTextures);
            _currentTextures = newTextures;
        }
    }

    if (entity->needsUpdateModelBounds()) {
        entity->updateModelBounds();
    }


    if (model->isVisible() != _visible) {
        // FIXME: this seems like it could be optimized if we tracked our last known visible state in
        //        the renderable item. As it stands now the model checks it's visible/invisible state
        //        so most of the time we don't do anything in this function.
        model->setVisibleInScene(_visible, scene);
    }

    //entity->doInitialModelSimulation();
    if (model->needsFixupInScene()) {
        model->removeFromScene(scene, transaction);
        render::Item::Status::Getters statusGetters;
        makeStatusGetters(entity, statusGetters);
        model->addToScene(scene, transaction, statusGetters);
    }

    // When the individual mesh parts of a model finish fading, they will mark their Model as needing updating
    // we will watch for that and ask the model to update it's render items
    if (model->getRenderItemsNeedUpdate()) {
        qDebug() << "QQQ" << __FUNCTION__ << "Update model render items" << model->getURL();
        model->updateRenderItems();
    }

    // make a copy of the animation properites
    auto newAnimationProperties = entity->getAnimationProperties();
    if (newAnimationProperties != _renderAnimationProperties) {
        withWriteLock([&] {
            _renderAnimationProperties = newAnimationProperties;
            _currentFrame = _renderAnimationProperties.getCurrentFrame();
        });
    }


    if (_animating) {
        if (!jointsMapped()) {
            mapJoints(entity, model->getJointNames());
        }
        animate(entity);
    }
}

// NOTE: this only renders the "meta" portion of the Model, namely it renders debugging items
void ModelEntityRenderer::doRender(RenderArgs* args) {
    PROFILE_RANGE(render_detail, "MetaModelRender");
    PerformanceTimer perfTimer("RMEIrender");

    ModelPointer model;
    withReadLock([&]{
        model = _model;
    });

    // this simple logic should say we set showingEntityHighlight to true whenever we are in marketplace mode and we have a marketplace id, or
    // whenever we are not set to none and shouldHighlight is true.
    bool showingEntityHighlight = ((bool)(args->_outlineFlags & (int)RenderArgs::RENDER_OUTLINE_MARKETPLACE_MODE) && _marketplaceEntity != 0) ||
                                  (args->_outlineFlags != RenderArgs::RENDER_OUTLINE_NONE && _shouldHighlight);
    if (showingEntityHighlight) {
        static glm::vec4 yellowColor(1.0f, 1.0f, 0.0f, 1.0f);
        gpu::Batch& batch = *args->_batch;
        batch.setModelTransform(_modelTransform); // we want to include the scale as well
        DependencyManager::get<GeometryCache>()->renderWireCubeInstance(args, batch, yellowColor);
    }

    if (_model && _model->didVisualGeometryRequestFail()) {
        static glm::vec4 greenColor(0.0f, 1.0f, 0.0f, 1.0f);
        gpu::Batch& batch = *args->_batch;
        batch.setModelTransform(_modelTransform); // we want to include the scale as well
        DependencyManager::get<GeometryCache>()->renderWireCubeInstance(args, batch, greenColor);
        return;
    }


    // Enqueue updates for the next frame
#if WANT_EXTRA_DEBUGGING
    // debugging...
    gpu::Batch& batch = *args->_batch;
    _model->renderDebugMeshBoxes(batch);
#endif

    // Remap textures for the next frame to avoid flicker
    // remapTextures();

#if 0
    // update whether the model should be showing collision mesh (this may flag for fixupInScene)
    bool showingCollisionGeometry = (bool)(args->_debugFlags & (int)RenderArgs::RENDER_DEBUG_HULLS);
    if (showingCollisionGeometry != _showCollisionGeometry) {
        ShapeType type = _entity->getShapeType();
        _showCollisionGeometry = showingCollisionGeometry;
        if (_showCollisionGeometry && type != SHAPE_TYPE_STATIC_MESH && type != SHAPE_TYPE_NONE) {
            // NOTE: it is OK if _collisionMeshKey is nullptr
            model::MeshPointer mesh = collisionMeshCache.getMesh(_collisionMeshKey);
            // NOTE: the model will render the collisionGeometry if it has one
            _model->setCollisionMesh(mesh);
        } else {
            // release mesh
            if (_collisionMeshKey) {
                collisionMeshCache.releaseMesh(_collisionMeshKey);
            }
            // clear model's collision geometry
            model::MeshPointer mesh = nullptr;
            _model->setCollisionMesh(mesh);
        }
    }
#endif
}

void ModelEntityRenderer::mapJoints(const TypedEntityPointer& entity, const QStringList& modelJointNames) {
    // if we don't have animation, or we're already joint mapped then bail early
    if (!entity->hasAnimation() || jointsMapped()) {
        return;
    }

    if (!_animation || _animation->getURL().toString() != entity->getAnimationURL()) {
        _animation = DependencyManager::get<AnimationCache>()->getAnimation(entity->getAnimationURL());
    }

    if (_animation && _animation->isLoaded()) {
        QStringList animationJointNames = _animation->getJointNames();

        if (modelJointNames.size() > 0 && animationJointNames.size() > 0) {
            _jointMapping.resize(modelJointNames.size());
            for (int i = 0; i < modelJointNames.size(); i++) {
                _jointMapping[i] = animationJointNames.indexOf(modelJointNames[i]);
            }
            _jointMappingCompleted = true;
        }
    }
}

