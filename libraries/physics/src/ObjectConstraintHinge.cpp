//
//  ObjectConstraintHinge.cpp
//  libraries/physics/src
//
//  Created by Seth Alves 2017-4-11
//  Copyright 2017 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include "QVariantGLM.h"

#include "EntityTree.h"
#include "ObjectConstraintHinge.h"
#include "PhysicsLogging.h"


const uint16_t ObjectConstraintHinge::constraintVersion = 1;
const glm::vec3 DEFAULT_HINGE_AXIS(1.0f, 0.0f, 0.0f);

ObjectConstraintHinge::ObjectConstraintHinge(const QUuid& id, EntityItemPointer ownerEntity) :
    ObjectConstraint(DYNAMIC_TYPE_HINGE, id, ownerEntity),
    _axisInA(DEFAULT_HINGE_AXIS),
    _axisInB(DEFAULT_HINGE_AXIS)
{
    #if WANT_DEBUG
    qCDebug(physics) << "ObjectConstraintHinge::ObjectConstraintHinge";
    #endif
}

ObjectConstraintHinge::~ObjectConstraintHinge() {
    #if WANT_DEBUG
    qCDebug(physics) << "ObjectConstraintHinge::~ObjectConstraintHinge";
    #endif
}

QList<btRigidBody*> ObjectConstraintHinge::getRigidBodies() {
    QList<btRigidBody*> result;
    result += getRigidBody();
    QUuid otherEntityID;
    withReadLock([&]{
        otherEntityID = _otherEntityID;
    });
    if (!otherEntityID.isNull()) {
        result += getOtherRigidBody(otherEntityID);
    }
    return result;
}

void ObjectConstraintHinge::prepareForPhysicsSimulation() {
}

void ObjectConstraintHinge::updateHinge() {
    btHingeConstraint* constraint { nullptr };
    glm::vec3 axisInA;
    float low;
    float high;
    float softness;
    float biasFactor;
    float relaxationFactor;

    withReadLock([&]{
        axisInA = _axisInA;
        constraint = static_cast<btHingeConstraint*>(_constraint);
        low = _low;
        high = _high;
        biasFactor = _biasFactor;
        relaxationFactor = _relaxationFactor;
        softness = _softness;
    });

    if (!constraint) {
        return;
    }

    constraint->setLimit(low, high, softness, biasFactor, relaxationFactor);
}


btTypedConstraint* ObjectConstraintHinge::getConstraint() {
    btHingeConstraint* constraint { nullptr };
    QUuid otherEntityID;
    glm::vec3 pivotInA;
    glm::vec3 axisInA;
    glm::vec3 pivotInB;
    glm::vec3 axisInB;

    withReadLock([&]{
        constraint = static_cast<btHingeConstraint*>(_constraint);
        pivotInA = _pivotInA;
        axisInA = _axisInA;
        otherEntityID = _otherEntityID;
        pivotInB = _pivotInB;
        axisInB = _axisInB;
    });
    if (constraint) {
        return constraint;
    }

    btRigidBody* rigidBodyA = getRigidBody();
    if (!rigidBodyA) {
        qCDebug(physics) << "ObjectConstraintHinge::getConstraint -- no rigidBodyA";
        return nullptr;
    }

    if (glm::length(axisInA) < FLT_EPSILON) {
        qCWarning(physics) << "hinge axis cannot be a zero vector";
        axisInA = DEFAULT_HINGE_AXIS;
    } else {
        axisInA = glm::normalize(axisInA);
    }

    if (!otherEntityID.isNull()) {
        // This hinge is between two entities... find the other rigid body.
        btRigidBody* rigidBodyB = getOtherRigidBody(otherEntityID);
        if (!rigidBodyB) {
            return nullptr;
        }

        if (glm::length(axisInB) < FLT_EPSILON) {
            qCWarning(physics) << "hinge axis cannot be a zero vector";
            axisInB = DEFAULT_HINGE_AXIS;
        } else {
            axisInB = glm::normalize(axisInB);
        }

        constraint = new btHingeConstraint(*rigidBodyA, *rigidBodyB,
                                           glmToBullet(pivotInA), glmToBullet(pivotInB),
                                           glmToBullet(axisInA), glmToBullet(axisInB),
                                           true); // useReferenceFrameA

    } else {
        // This hinge is between an entity and the world-frame.
        constraint = new btHingeConstraint(*rigidBodyA,
                                           glmToBullet(pivotInA), glmToBullet(axisInA),
                                           true); // useReferenceFrameA
    }

    withWriteLock([&]{
        _constraint = constraint;
    });

    // if we don't wake up rigidBodyA, we may not send the dynamicData property over the network
    forceBodyNonStatic();
    activateBody();

    updateHinge();

    return constraint;
}


bool ObjectConstraintHinge::updateArguments(QVariantMap arguments) {
    glm::vec3 pivotInA;
    glm::vec3 axisInA;
    QUuid otherEntityID;
    glm::vec3 pivotInB;
    glm::vec3 axisInB;
    float low;
    float high;
    float softness;
    float biasFactor;
    float relaxationFactor;

    bool needUpdate = false;
    bool somethingChanged = ObjectDynamic::updateArguments(arguments);
    withReadLock([&]{
        bool ok = true;
        pivotInA = EntityDynamicInterface::extractVec3Argument("hinge constraint", arguments, "pivot", ok, false);
        if (!ok) {
            pivotInA = _pivotInA;
        }

        ok = true;
        axisInA = EntityDynamicInterface::extractVec3Argument("hinge constraint", arguments, "axis", ok, false);
        if (!ok) {
            axisInA = _axisInA;
        }

        ok = true;
        otherEntityID = QUuid(EntityDynamicInterface::extractStringArgument("hinge constraint",
                                                                            arguments, "otherEntityID", ok, false));
        if (!ok) {
            otherEntityID = _otherEntityID;
        }

        ok = true;
        pivotInB = EntityDynamicInterface::extractVec3Argument("hinge constraint", arguments, "otherPivot", ok, false);
        if (!ok) {
            pivotInB = _pivotInB;
        }

        ok = true;
        axisInB = EntityDynamicInterface::extractVec3Argument("hinge constraint", arguments, "otherAxis", ok, false);
        if (!ok) {
            axisInB = _axisInB;
        }

        ok = true;
        low = EntityDynamicInterface::extractFloatArgument("hinge constraint", arguments, "low", ok, false);
        if (!ok) {
            low = _low;
        }

        ok = true;
        high = EntityDynamicInterface::extractFloatArgument("hinge constraint", arguments, "high", ok, false);
        if (!ok) {
            high = _high;
        }

        ok = true;
        softness = EntityDynamicInterface::extractFloatArgument("hinge constraint", arguments, "softness", ok, false);
        if (!ok) {
            softness = _softness;
        }

        ok = true;
        biasFactor = EntityDynamicInterface::extractFloatArgument("hinge constraint", arguments, "biasFactor", ok, false);
        if (!ok) {
            biasFactor = _biasFactor;
        }

        ok = true;
        relaxationFactor = EntityDynamicInterface::extractFloatArgument("hinge constraint", arguments,
                                                                        "relaxationFactor", ok, false);
        if (!ok) {
            relaxationFactor = _relaxationFactor;
        }

        if (somethingChanged ||
            pivotInA != _pivotInA ||
            axisInA != _axisInA ||
            otherEntityID != _otherEntityID ||
            pivotInB != _pivotInB ||
            axisInB != _axisInB ||
            low != _low ||
            high != _high ||
            softness != _softness ||
            biasFactor != _biasFactor ||
            relaxationFactor != _relaxationFactor) {
            // something changed
            needUpdate = true;
        }
    });

    if (needUpdate) {
        withWriteLock([&] {
            _pivotInA = pivotInA;
            _axisInA = axisInA;
            _otherEntityID = otherEntityID;
            _pivotInB = pivotInB;
            _axisInB = axisInB;
            _low = low;
            _high = high;
            _softness = softness;
            _biasFactor = biasFactor;
            _relaxationFactor = relaxationFactor;

            _active = true;

            auto ownerEntity = _ownerEntity.lock();
            if (ownerEntity) {
                ownerEntity->setDynamicDataDirty(true);
                ownerEntity->setDynamicDataNeedsTransmit(true);
            }
        });

        updateHinge();
    }

    return true;
}

QVariantMap ObjectConstraintHinge::getArguments() {
    QVariantMap arguments = ObjectDynamic::getArguments();
    withReadLock([&] {
        if (_constraint) {
            arguments["pivot"] = glmToQMap(_pivotInA);
            arguments["axis"] = glmToQMap(_axisInA);
            arguments["otherEntityID"] = _otherEntityID;
            arguments["otherPivot"] = glmToQMap(_pivotInB);
            arguments["otherAxis"] = glmToQMap(_axisInB);
            arguments["low"] = _low;
            arguments["high"] = _high;
            arguments["softness"] = _softness;
            arguments["biasFactor"] = _biasFactor;
            arguments["relaxationFactor"] = _relaxationFactor;
            arguments["angle"] = static_cast<btHingeConstraint*>(_constraint)->getHingeAngle(); // [-PI,PI]
        }
    });
    return arguments;
}

QByteArray ObjectConstraintHinge::serialize() const {
    QByteArray serializedConstraintArguments;
    QDataStream dataStream(&serializedConstraintArguments, QIODevice::WriteOnly);

    dataStream << DYNAMIC_TYPE_HINGE;
    dataStream << getID();
    dataStream << ObjectConstraintHinge::constraintVersion;

    withReadLock([&] {
        dataStream << _pivotInA;
        dataStream << _axisInA;
        dataStream << _otherEntityID;
        dataStream << _pivotInB;
        dataStream << _axisInB;
        dataStream << _low;
        dataStream << _high;
        dataStream << _softness;
        dataStream << _biasFactor;
        dataStream << _relaxationFactor;

        dataStream << localTimeToServerTime(_expires);
        dataStream << _tag;
    });

    return serializedConstraintArguments;
}

void ObjectConstraintHinge::deserialize(QByteArray serializedArguments) {
    QDataStream dataStream(serializedArguments);

    EntityDynamicType type;
    dataStream >> type;
    assert(type == getType());

    QUuid id;
    dataStream >> id;
    assert(id == getID());

    uint16_t serializationVersion;
    dataStream >> serializationVersion;
    if (serializationVersion != ObjectConstraintHinge::constraintVersion) {
        assert(false);
        return;
    }

    withWriteLock([&] {
        dataStream >> _pivotInA;
        dataStream >> _axisInA;
        dataStream >> _otherEntityID;
        dataStream >> _pivotInB;
        dataStream >> _axisInB;
        dataStream >> _low;
        dataStream >> _high;
        dataStream >> _softness;
        dataStream >> _biasFactor;
        dataStream >> _relaxationFactor;

        quint64 serverExpires;
        dataStream >> serverExpires;
        _expires = serverTimeToLocalTime(serverExpires);

        dataStream >> _tag;

        _active = true;
    });
}
