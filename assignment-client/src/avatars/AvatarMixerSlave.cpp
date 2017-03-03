//
//  AvatarMixerSlave.cpp
//  assignment-client/src/avatar
//
//  Created by Brad Hefta-Gaub on 2/14/2017.
//  Copyright 2017 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include <algorithm>
#include <random>

#include <glm/glm.hpp>
#include <glm/gtx/norm.hpp>
#include <glm/gtx/vector_angle.hpp>

#include <AvatarLogging.h>
#include <LogHandler.h>
#include <NetworkAccessManager.h>
#include <NodeList.h>
#include <Node.h>
#include <OctreeConstants.h>
#include <udt/PacketHeaders.h>
#include <SharedUtil.h>
#include <StDev.h>
#include <UUID.h>


#include "AvatarMixer.h"
#include "AvatarMixerClientData.h"
#include "AvatarMixerSlave.h"


void AvatarMixerSlave::configure(ConstIter begin, ConstIter end) {
    _begin = begin;
    _end = end;
}

void AvatarMixerSlave::configureBroadcast(ConstIter begin, ConstIter end, 
                                p_high_resolution_clock::time_point lastFrameTimestamp,
                                float maxKbpsPerNode, float throttlingRatio) {
    _begin = begin;
    _end = end;
    _lastFrameTimestamp = lastFrameTimestamp;
    _maxKbpsPerNode = maxKbpsPerNode;
    _throttlingRatio = throttlingRatio;
}

void AvatarMixerSlave::harvestStats(AvatarMixerSlaveStats& stats) {
    stats = _stats;
    _stats.reset();
}


void AvatarMixerSlave::processIncomingPackets(const SharedNodePointer& node) {
    auto start = usecTimestampNow();
    auto nodeData = dynamic_cast<AvatarMixerClientData*>(node->getLinkedData());
    if (nodeData) {
        _stats.nodesProcessed++;
        _stats.packetsProcessed += nodeData->processPackets();
    }
    auto end = usecTimestampNow();
    _stats.processIncomingPacketsElapsedTime += (end - start);
}


int AvatarMixerSlave::sendIdentityPacket(const AvatarMixerClientData* nodeData, const SharedNodePointer& destinationNode) {
    int bytesSent = 0;
    QByteArray individualData = nodeData->getConstAvatarData()->identityByteArray();
    auto identityPacket = NLPacket::create(PacketType::AvatarIdentity, individualData.size());
    individualData.replace(0, NUM_BYTES_RFC4122_UUID, nodeData->getNodeID().toRfc4122()); // FIXME, this looks suspicious
    bytesSent += individualData.size();
    identityPacket->write(individualData);
    DependencyManager::get<NodeList>()->sendPacket(std::move(identityPacket), *destinationNode);
    _stats.numIdentityPackets++;
    return bytesSent;
}

static const int AVATAR_MIXER_BROADCAST_FRAMES_PER_SECOND = 45;

// FIXME - There is some old logic (unchanged as of 2/17/17) that randomly decides to send an identity
// packet. That logic had the following comment about the constants it uses...
//
//         An 80% chance of sending a identity packet within a 5 second interval.
//         assuming 60 htz update rate.
//
// Assuming the calculation of the constant is in fact correct for 80% and 60hz and 5 seconds (an assumption
// that I have not verified) then the constant is definitely wrong now, since we send at 45hz.
const float IDENTITY_SEND_PROBABILITY = 1.0f / 187.0f;

void AvatarMixerSlave::broadcastAvatarData(const SharedNodePointer& node) {
    quint64 start = usecTimestampNow();

    auto nodeList = DependencyManager::get<NodeList>();

    // setup for distributed random floating point values
    std::random_device randomDevice;
    std::mt19937 generator(randomDevice());
    std::uniform_real_distribution<float> distribution;

    if (node->getLinkedData() && (node->getType() == NodeType::Agent) && node->getActiveSocket()) {
        _stats.nodesBroadcastedTo++;

        AvatarMixerClientData* nodeData = reinterpret_cast<AvatarMixerClientData*>(node->getLinkedData());

        nodeData->resetInViewStats();

        const AvatarData& avatar = nodeData->getAvatar();
        glm::vec3 myPosition = avatar.getClientGlobalPosition();

        // reset the internal state for correct random number distribution
        distribution.reset();

        // reset the number of sent avatars
        nodeData->resetNumAvatarsSentLastFrame();

        // keep a counter of the number of considered avatars
        int numOtherAvatars = 0;

        // keep track of outbound data rate specifically for avatar data
        int numAvatarDataBytes = 0;
        int identityBytesSent = 0;

        // max number of avatarBytes per frame
        auto maxAvatarBytesPerFrame = (_maxKbpsPerNode * BYTES_PER_KILOBIT) / AVATAR_MIXER_BROADCAST_FRAMES_PER_SECOND;

        // FIXME - find a way to not send the sessionID for every avatar
        int minimumBytesPerAvatar = AvatarDataPacket::AVATAR_HAS_FLAGS_SIZE + NUM_BYTES_RFC4122_UUID;

        int overBudgetAvatars = 0;

        // keep track of the number of other avatars held back in this frame
        int numAvatarsHeldBack = 0;

        // keep track of the number of other avatar frames skipped
        int numAvatarsWithSkippedFrames = 0;

        // When this is true, the AvatarMixer will send Avatar data to a client about avatars that are not in the view frustrum
        bool getsOutOfView = nodeData->getRequestsDomainListData();

        // When this is true, the AvatarMixer will send Avatar data to a client about avatars that they've ignored
        bool getsIgnoredByMe = getsOutOfView;

        // When this is true, the AvatarMixer will send Avatar data to a client about avatars that have ignored them
        bool getsAnyIgnored = getsIgnoredByMe && node->getCanKick();

        // setup a PacketList for the avatarPackets
        auto avatarPacketList = NLPacketList::create(PacketType::BulkAvatarData);

        // Define the minimum bubble size
        static const glm::vec3 minBubbleSize = glm::vec3(0.3f, 1.3f, 0.3f);
        // Define the scale of the box for the current node
        glm::vec3 nodeBoxScale = (nodeData->getPosition() - nodeData->getGlobalBoundingBoxCorner()) * 2.0f;
        // Set up the bounding box for the current node
        AABox nodeBox(nodeData->getGlobalBoundingBoxCorner(), nodeBoxScale);
        // Clamp the size of the bounding box to a minimum scale
        if (glm::any(glm::lessThan(nodeBoxScale, minBubbleSize))) {
            nodeBox.setScaleStayCentered(minBubbleSize);
        }
        // Quadruple the scale of both bounding boxes
        nodeBox.embiggen(4.0f);


        // setup list of AvatarData as well as maps to map betweeen the AvatarData and the original nodes
        // for calling the AvatarData::sortAvatars() function and getting our sorted list of client nodes
        QList<AvatarSharedPointer> avatarList;
        std::unordered_map<AvatarSharedPointer, SharedNodePointer> avatarDataToNodes;

        int listItem = 0;
        std::for_each(_begin, _end, [&](const SharedNodePointer& otherNode) {
            const AvatarMixerClientData* otherNodeData = reinterpret_cast<const AvatarMixerClientData*>(otherNode->getLinkedData());

            // theoretically it's possible for a Node to be in the NodeList (and therefore end up here),
            // but not have yet sent data that's linked to the node. Check for that case and don't
            // consider those nodes.
            if (otherNodeData) {
                listItem++;
                AvatarSharedPointer otherAvatar = otherNodeData->getAvatarSharedPointer();
                avatarList << otherAvatar;
                avatarDataToNodes[otherAvatar] = otherNode;
            }
        });

        AvatarSharedPointer thisAvatar = nodeData->getAvatarSharedPointer();
        ViewFrustum cameraView = nodeData->getViewFrustom();
        std::priority_queue<AvatarPriority> sortedAvatars = AvatarData::sortAvatars(
                avatarList, cameraView,

                [&](AvatarSharedPointer avatar)->uint64_t{
                    auto avatarNode = avatarDataToNodes[avatar];
                    assert(avatarNode); // we can't have gotten here without the avatarData being a valid key in the map
                    return nodeData->getLastBroadcastTime(avatarNode->getUUID());
                },

                [&](AvatarSharedPointer avatar)->float{
                    glm::vec3 nodeBoxHalfScale = (avatar->getPosition() - avatar->getGlobalBoundingBoxCorner());
                    return glm::max(nodeBoxHalfScale.x, glm::max(nodeBoxHalfScale.y, nodeBoxHalfScale.z));
                },

                [&](AvatarSharedPointer avatar)->bool{
                    if (avatar == thisAvatar) {
                        return true; // ignore ourselves...
                    }

                    bool shouldIgnore = false;

                    // We will also ignore other nodes for a couple of different reasons:
                    //   1) ignore bubbles and ignore specific node
                    //   2) the node hasn't really updated it's frame data recently, this can
                    //      happen if for example the avatar is connected on a desktop and sending
                    //      updates at ~30hz. So every 3 frames we skip a frame.
                    auto avatarNode = avatarDataToNodes[avatar];

                    assert(avatarNode); // we can't have gotten here without the avatarData being a valid key in the map

                    const AvatarMixerClientData* avatarNodeData = reinterpret_cast<const AvatarMixerClientData*>(avatarNode->getLinkedData());
                    assert(avatarNodeData); // we can't have gotten here without avatarNode having valid data
                    quint64 startIgnoreCalculation = usecTimestampNow();

                    // make sure we have data for this avatar, that it isn't the same node,
                    // and isn't an avatar that the viewing node has ignored
                    // or that has ignored the viewing node
                    if (!avatarNode->getLinkedData()
                        || avatarNode->getUUID() == node->getUUID()
                        || (node->isIgnoringNodeWithID(avatarNode->getUUID()) && !getsIgnoredByMe)
                        || (avatarNode->isIgnoringNodeWithID(node->getUUID()) && !getsAnyIgnored)) {
                        shouldIgnore = true;
                    } else {

                        // Check to see if the space bubble is enabled
                        if (node->isIgnoreRadiusEnabled() || avatarNode->isIgnoreRadiusEnabled()) {

                            // Define the scale of the box for the current other node
                            glm::vec3 otherNodeBoxScale = (avatarNodeData->getPosition() - avatarNodeData->getGlobalBoundingBoxCorner()) * 2.0f;
                            // Set up the bounding box for the current other node
                            AABox otherNodeBox(avatarNodeData->getGlobalBoundingBoxCorner(), otherNodeBoxScale);
                            // Clamp the size of the bounding box to a minimum scale
                            if (glm::any(glm::lessThan(otherNodeBoxScale, minBubbleSize))) {
                                otherNodeBox.setScaleStayCentered(minBubbleSize);
                            }
                            // Quadruple the scale of both bounding boxes
                            otherNodeBox.embiggen(4.0f);

                            // Perform the collision check between the two bounding boxes
                            if (nodeBox.touches(otherNodeBox)) {
                                nodeData->ignoreOther(node, avatarNode);
                                shouldIgnore = !getsAnyIgnored;
                            }
                        }
                        // Not close enough to ignore
                        if (!shouldIgnore) {
                            nodeData->removeFromRadiusIgnoringSet(node, avatarNode->getUUID());
                        }
                    }
                    quint64 endIgnoreCalculation = usecTimestampNow();
                    _stats.ignoreCalculationElapsedTime += (endIgnoreCalculation - startIgnoreCalculation);

                    if (!shouldIgnore) {
                        AvatarDataSequenceNumber lastSeqToReceiver = nodeData->getLastBroadcastSequenceNumber(avatarNode->getUUID());
                        AvatarDataSequenceNumber lastSeqFromSender = avatarNodeData->getLastReceivedSequenceNumber();

                        // FIXME - This code does appear to be working. But it seems brittle.
                        //         It supports determining if the frame of data for this "other"
                        //         avatar has already been sent to the reciever. This has been
                        //         verified to work on a desktop display that renders at 60hz and
                        //         therefore sends to mixer at 30hz. Each second you'd expect to
                        //         have 15 (45hz-30hz) duplicate frames. In this case, the stat
                        //         avg_other_av_skips_per_second does report 15.
                        //
                        // make sure we haven't already sent this data from this sender to this receiver
                        // or that somehow we haven't sent
                        if (lastSeqToReceiver == lastSeqFromSender && lastSeqToReceiver != 0) {
                            ++numAvatarsHeldBack;
                            shouldIgnore = true;
                        } else if (lastSeqFromSender - lastSeqToReceiver > 1) {
                            // this is a skip - we still send the packet but capture the presence of the skip so we see it happening
                            ++numAvatarsWithSkippedFrames;
                        }
                    }
                    return shouldIgnore;
                });

        // loop through our sorted avatars and allocate our bandwidth to them accordingly
        int avatarRank = 0;

        // this is overly conservative, because it includes some avatars we might not consider
        int remainingAvatars = (int)sortedAvatars.size(); 

        while (!sortedAvatars.empty()) {
            AvatarPriority sortData = sortedAvatars.top();
            sortedAvatars.pop();
            const auto& avatarData = sortData.avatar;
            avatarRank++;
            remainingAvatars--;

            auto otherNode = avatarDataToNodes[avatarData];
            assert(otherNode); // we can't have gotten here without the avatarData being a valid key in the map

            // NOTE: Here's where we determine if we are over budget and drop to bare minimum data
            int minimRemainingAvatarBytes = minimumBytesPerAvatar * remainingAvatars;
            bool overBudget = (identityBytesSent + numAvatarDataBytes + minimRemainingAvatarBytes) > maxAvatarBytesPerFrame;

            quint64 startAvatarDataPacking = usecTimestampNow();

            ++numOtherAvatars;

            const AvatarMixerClientData* otherNodeData = reinterpret_cast<const AvatarMixerClientData*>(otherNode->getLinkedData());

            // make sure we send out identity packets to and from new arrivals.
            bool forceSend = !nodeData->checkAndSetHasReceivedFirstPacketsFrom(otherNode->getUUID());

            // FIXME - this clause seems suspicious "... || otherNodeData->getIdentityChangeTimestamp() > _lastFrameTimestamp ..."
            if (!overBudget
                && otherNodeData->getIdentityChangeTimestamp().time_since_epoch().count() > 0
                && (forceSend
                || otherNodeData->getIdentityChangeTimestamp() > _lastFrameTimestamp
                || distribution(generator) < IDENTITY_SEND_PROBABILITY)) {

                identityBytesSent += sendIdentityPacket(otherNodeData, node);
            }

            const AvatarData* otherAvatar = otherNodeData->getConstAvatarData();
            glm::vec3 otherPosition = otherAvatar->getClientGlobalPosition();

            // determine if avatar is in view, to determine how much data to include...
            glm::vec3 otherNodeBoxScale = (otherPosition - otherNodeData->getGlobalBoundingBoxCorner()) * 2.0f;
            AABox otherNodeBox(otherNodeData->getGlobalBoundingBoxCorner(), otherNodeBoxScale);
            bool isInView = nodeData->otherAvatarInView(otherNodeBox);

            // start a new segment in the PacketList for this avatar
            avatarPacketList->startSegment();

            AvatarData::AvatarDataDetail detail;

            if (overBudget) {
                overBudgetAvatars++;
                _stats.overBudgetAvatars++;
                detail = AvatarData::NoData;
            } else  if (!isInView && !getsOutOfView) {
                detail = AvatarData::NoData;
                nodeData->incrementAvatarOutOfView();
            } else {
                detail = distribution(generator) < AVATAR_SEND_FULL_UPDATE_RATIO
                    ? AvatarData::SendAllData : AvatarData::CullSmallData;
                nodeData->incrementAvatarInView();
            }

            bool includeThisAvatar = true;
            auto lastEncodeForOther = nodeData->getLastOtherAvatarEncodeTime(otherNode->getUUID());
            QVector<JointData>& lastSentJointsForOther = nodeData->getLastOtherAvatarSentJoints(otherNode->getUUID());
            bool distanceAdjust = true;
            glm::vec3 viewerPosition = myPosition;
            AvatarDataPacket::HasFlags hasFlagsOut; // the result of the toByteArray
            bool dropFaceTracking = false;

            quint64 start = usecTimestampNow();
            QByteArray bytes = otherAvatar->toByteArray(detail, lastEncodeForOther, lastSentJointsForOther, 
                                            hasFlagsOut, dropFaceTracking, distanceAdjust, viewerPosition, &lastSentJointsForOther);
            quint64 end = usecTimestampNow();
            _stats.toByteArrayElapsedTime += (end - start);

            static const int MAX_ALLOWED_AVATAR_DATA = (1400 - NUM_BYTES_RFC4122_UUID);
            if (bytes.size() > MAX_ALLOWED_AVATAR_DATA) {
                qCWarning(avatars) << "otherAvatar.toByteArray() resulted in very large buffer:" << bytes.size() << "... attempt to drop facial data";

                dropFaceTracking = true; // first try dropping the facial data
                bytes = otherAvatar->toByteArray(detail, lastEncodeForOther, lastSentJointsForOther,
                    hasFlagsOut, dropFaceTracking, distanceAdjust, viewerPosition, &lastSentJointsForOther);

                if (bytes.size() > MAX_ALLOWED_AVATAR_DATA) {
                    qCWarning(avatars) << "otherAvatar.toByteArray() without facial data resulted in very large buffer:" << bytes.size() << "... reduce to MinimumData";
                    bytes = otherAvatar->toByteArray(AvatarData::MinimumData, lastEncodeForOther, lastSentJointsForOther,
                        hasFlagsOut, dropFaceTracking, distanceAdjust, viewerPosition, &lastSentJointsForOther);
                }

                if (bytes.size() > MAX_ALLOWED_AVATAR_DATA) {
                    qCWarning(avatars) << "otherAvatar.toByteArray() MinimumData resulted in very large buffer:" << bytes.size() << "... FAIL!!";
                    includeThisAvatar = false;
                }
            }

            if (includeThisAvatar) {
                numAvatarDataBytes += avatarPacketList->write(otherNode->getUUID().toRfc4122());
                numAvatarDataBytes += avatarPacketList->write(bytes);

                if (detail != AvatarData::NoData) {
                    _stats.numOthersIncluded++;

                    // increment the number of avatars sent to this reciever
                    nodeData->incrementNumAvatarsSentLastFrame();

                    // set the last sent sequence number for this sender on the receiver
                    nodeData->setLastBroadcastSequenceNumber(otherNode->getUUID(), 
                                    otherNodeData->getLastReceivedSequenceNumber());

                    // remember the last time we sent details about this other node to the receiver
                    nodeData->setLastBroadcastTime(otherNode->getUUID(), start);
                }
            }

            avatarPacketList->endSegment();

            quint64 endAvatarDataPacking = usecTimestampNow();
            _stats.avatarDataPackingElapsedTime += (endAvatarDataPacking - startAvatarDataPacking);
        };

        quint64 startPacketSending = usecTimestampNow();

        // close the current packet so that we're always sending something
        avatarPacketList->closeCurrentPacket(true);

        _stats.numPacketsSent += (int)avatarPacketList->getNumPackets();
        _stats.numBytesSent += numAvatarDataBytes;

        // send the avatar data PacketList
        nodeList->sendPacketList(std::move(avatarPacketList), *node);

        // record the bytes sent for other avatar data in the AvatarMixerClientData
        nodeData->recordSentAvatarData(numAvatarDataBytes);

        // record the number of avatars held back this frame
        nodeData->recordNumOtherAvatarStarves(numAvatarsHeldBack);
        nodeData->recordNumOtherAvatarSkips(numAvatarsWithSkippedFrames);

        quint64 endPacketSending = usecTimestampNow();
        _stats.packetSendingElapsedTime += (endPacketSending - startPacketSending);
    }

    quint64 end = usecTimestampNow();
    _stats.jobElapsedTime += (end - start);
}

