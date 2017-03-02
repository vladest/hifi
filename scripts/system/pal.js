"use strict";
/* jslint vars: true, plusplus: true, forin: true*/
/* globals Tablet, Script, AvatarList, Users, Entities, MyAvatar, Camera, Overlays, Vec3, Quat, Controller, print, getControllerWorldLocation */
/* eslint indent: ["error", 4, { "outerIIFEBody": 0 }] */
//
// pal.js
//
// Created by Howard Stearns on December 9, 2016
// Copyright 2016 High Fidelity, Inc
//
// Distributed under the Apache License, Version 2.0
// See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

(function() { // BEGIN LOCAL_SCOPE

// hardcoding these as it appears we cannot traverse the originalTextures in overlays???  Maybe I've missed
// something, will revisit as this is sorta horrible.
var UNSELECTED_TEXTURES = {
    "idle-D": Script.resolvePath("./assets/models/Avatar-Overlay-v1.fbx/Avatar-Overlay-v1.fbm/avatar-overlay-idle.png"),
    "idle-E": Script.resolvePath("./assets/models/Avatar-Overlay-v1.fbx/Avatar-Overlay-v1.fbm/avatar-overlay-idle.png")
};
var SELECTED_TEXTURES = {
    "idle-D": Script.resolvePath("./assets/models/Avatar-Overlay-v1.fbx/Avatar-Overlay-v1.fbm/avatar-overlay-selected.png"),
    "idle-E": Script.resolvePath("./assets/models/Avatar-Overlay-v1.fbx/Avatar-Overlay-v1.fbm/avatar-overlay-selected.png")
};
var HOVER_TEXTURES = {
    "idle-D": Script.resolvePath("./assets/models/Avatar-Overlay-v1.fbx/Avatar-Overlay-v1.fbm/avatar-overlay-hover.png"),
    "idle-E": Script.resolvePath("./assets/models/Avatar-Overlay-v1.fbx/Avatar-Overlay-v1.fbm/avatar-overlay-hover.png")
};

var UNSELECTED_COLOR = { red: 0x1F, green: 0xC6, blue: 0xA6};
var SELECTED_COLOR = {red: 0xF3, green: 0x91, blue: 0x29};
var HOVER_COLOR = {red: 0xD0, green: 0xD0, blue: 0xD0}; // almost white for now

var conserveResources = true;

Script.include("/~/system/libraries/controllers.js");

function projectVectorOntoPlane(normalizedVector, planeNormal) {
    return Vec3.cross(planeNormal, Vec3.cross(normalizedVector, planeNormal));
}
function angleBetweenVectorsInPlane(from, to, normal) {
    var projectedFrom = projectVectorOntoPlane(from, normal);
    var projectedTo = projectVectorOntoPlane(to, normal);
    return Vec3.orientedAngle(projectedFrom, projectedTo, normal);
}

//
// Overlays.
//
var overlays = {}; // Keeps track of all our extended overlay data objects, keyed by target identifier.

function ExtendedOverlay(key, type, properties, selected, hasModel) { // A wrapper around overlays to store the key it is associated with.
    overlays[key] = this;
    if (hasModel) {
        var modelKey = key + "-m";
        this.model = new ExtendedOverlay(modelKey, "model", {
            url: Script.resolvePath("./assets/models/Avatar-Overlay-v1.fbx"),
            textures: textures(selected),
            ignoreRayIntersection: true
        }, false, false);
    } else {
        this.model = undefined;
    }
    this.key = key;
    this.selected = selected || false; // not undefined
    this.hovering = false;
    this.activeOverlay = Overlays.addOverlay(type, properties); // We could use different overlays for (un)selected...
}
// Instance methods:
ExtendedOverlay.prototype.deleteOverlay = function () { // remove display and data of this overlay
    Overlays.deleteOverlay(this.activeOverlay);
    delete overlays[this.key];
};

ExtendedOverlay.prototype.editOverlay = function (properties) { // change display of this overlay
    Overlays.editOverlay(this.activeOverlay, properties);
};

function color(selected, hovering, level) {
    var base = hovering ? HOVER_COLOR : selected ? SELECTED_COLOR : UNSELECTED_COLOR;
    function scale(component) {
        var delta = 0xFF - component;
        return component + (delta * level);
    }
    return {red: scale(base.red), green: scale(base.green), blue: scale(base.blue)};
}

function textures(selected, hovering) {
    return hovering ? HOVER_TEXTURES : selected ? SELECTED_TEXTURES : UNSELECTED_TEXTURES;
}
// so we don't have to traverse the overlays to get the last one
var lastHoveringId = 0;
ExtendedOverlay.prototype.hover = function (hovering) {
    this.hovering = hovering;
    if (this.key === lastHoveringId) {
        if (hovering) {
            return;
        } else {
            lastHoveringId = 0;
        }
    }
    this.editOverlay({color: color(this.selected, hovering, this.audioLevel)});
    if (this.model) {
        this.model.editOverlay({textures: textures(this.selected, hovering)});
    }
    if (hovering) {
        // un-hover the last hovering overlay
        if (lastHoveringId && lastHoveringId !== this.key) {
            ExtendedOverlay.get(lastHoveringId).hover(false);
        }
        lastHoveringId = this.key;
    }
};
ExtendedOverlay.prototype.select = function (selected) {
    if (this.selected === selected) {
        return;
    }

    UserActivityLogger.palAction(selected ? "avatar_selected" : "avatar_deselected", this.key);

    this.editOverlay({color: color(selected, this.hovering, this.audioLevel)});
    if (this.model) {
        this.model.editOverlay({textures: textures(selected)});
    }
    this.selected = selected;
};
// Class methods:
var selectedIds = [];
ExtendedOverlay.isSelected = function (id) {
    return -1 !== selectedIds.indexOf(id);
};
ExtendedOverlay.get = function (key) { // answer the extended overlay data object associated with the given avatar identifier
    return overlays[key];
};
ExtendedOverlay.some = function (iterator) { // Bails early as soon as iterator returns truthy.
    var key;
    for (key in overlays) {
        if (iterator(ExtendedOverlay.get(key))) {
            return;
        }
    }
};
ExtendedOverlay.unHover = function () { // calls hover(false) on lastHoveringId (if any)
    if (lastHoveringId) {
        ExtendedOverlay.get(lastHoveringId).hover(false);
    }
};

// hit(overlay) on the one overlay intersected by pickRay, if any.
// noHit() if no ExtendedOverlay was intersected (helps with hover)
ExtendedOverlay.applyPickRay = function (pickRay, hit, noHit) {
    var pickedOverlay = Overlays.findRayIntersection(pickRay); // Depends on nearer coverOverlays to extend closer to us than farther ones.
    if (!pickedOverlay.intersects) {
        if (noHit) {
            return noHit();
        }
        return;
    }
    ExtendedOverlay.some(function (overlay) { // See if pickedOverlay is one of ours.
        if ((overlay.activeOverlay) === pickedOverlay.overlayID) {
            hit(overlay);
            return true;
        }
    });
};


//
// Similar, for entities
//
function HighlightedEntity(id, entityProperties) {
    this.id = id;
    this.overlay = Overlays.addOverlay('cube', {
        position: entityProperties.position,
        rotation: entityProperties.rotation,
        dimensions: entityProperties.dimensions,
        solid: false,
        color: {
            red: 0xF3,
            green: 0x91,
            blue: 0x29
        },
        lineWidth: 1.0,
        ignoreRayIntersection: true,
        drawInFront: false // Arguable. For now, let's not distract with mysterious wires around the scene.
    });
    HighlightedEntity.overlays.push(this);
}
HighlightedEntity.overlays = [];
HighlightedEntity.clearOverlays = function clearHighlightedEntities() {
    HighlightedEntity.overlays.forEach(function (highlighted) {
        Overlays.deleteOverlay(highlighted.overlay);
    });
    HighlightedEntity.overlays = [];
};
HighlightedEntity.updateOverlays = function updateHighlightedEntities() {
    HighlightedEntity.overlays.forEach(function (highlighted) {
        var properties = Entities.getEntityProperties(highlighted.id, ['position', 'rotation', 'dimensions']);
        Overlays.editOverlay(highlighted.overlay, {
            position: properties.position,
            rotation: properties.rotation,
            dimensions: properties.dimensions
        });
    });
};

function fromQml(message) { // messages are {method, params}, like json-rpc. See also sendToQml.
    var data;
    switch (message.method) {
    case 'selected':
        selectedIds = message.params;
        ExtendedOverlay.some(function (overlay) {
            var id = overlay.key;
            var selected = ExtendedOverlay.isSelected(id);
            overlay.select(selected);
        });

        HighlightedEntity.clearOverlays();
        if (selectedIds.length) {
            Entities.findEntitiesInFrustum(Camera.frustum).forEach(function (id) {
                // Because lastEditedBy is per session, the vast majority of entities won't match,
                // so it would probably be worth reducing marshalling costs by asking for just we need.
                // However, providing property name(s) is advisory and some additional properties are
                // included anyway. As it turns out, asking for 'lastEditedBy' gives 'position', 'rotation',
                // and 'dimensions', too, so we might as well make use of them instead of making a second
                // getEntityProperties call.
                // It would be nice if we could harden this against future changes by specifying all
                // and only these four in an array, but see
                // https://highfidelity.fogbugz.com/f/cases/2728/Entities-getEntityProperties-id-lastEditedBy-name-lastEditedBy-doesn-t-work
                var properties = Entities.getEntityProperties(id, 'lastEditedBy');
                if (ExtendedOverlay.isSelected(properties.lastEditedBy)) {
                    new HighlightedEntity(id, properties);
                }
            });
        }
        break;
    case 'refresh':
        removeOverlays();
        // If filter is specified from .qml instead of through settings, update the settings.
        if (message.params.filter !== undefined) {
            Settings.setValue('pal/filtered', !!message.params.filter);
        }
        populateUserList(message.params.selected);
        UserActivityLogger.palAction("refresh", "");
        break;
    case 'updateGain':
        data = message.params;
        if (data['isReleased']) {
            // isReleased=true happens once at the end of a cycle of dragging
            // the slider about, but with same gain as last isReleased=false so
            // we don't set the gain in that case, and only here do we want to
            // send an analytic event.
            UserActivityLogger.palAction("avatar_gain_changed", data['sessionId']);
        } else {
            Users.setAvatarGain(data['sessionId'], data['gain']);
        }
        break;
    case 'displayNameUpdate':
        if (MyAvatar.displayName !== message.params) {
            MyAvatar.displayName = message.params;
            UserActivityLogger.palAction("display_name_change", "");
        }
        break;
    default:
        print('Unrecognized message from Pal.qml:', JSON.stringify(message));
    }
}

function sendToQml(message) {
    tablet.sendToQml(message);
}

//
// Main operations.
//
function addAvatarNode(id) {
    var selected = ExtendedOverlay.isSelected(id);
    return new ExtendedOverlay(id, "sphere", {
        drawInFront: true,
        solid: true,
        alpha: 0.8,
        color: color(selected, false, 0.0),
        ignoreRayIntersection: false}, selected, !conserveResources);
}
// Each open/refresh will capture a stable set of avatarsOfInterest, within the specified filter.
var avatarsOfInterest = {};
function populateUserList(selectData) {
    var filter = Settings.getValue('pal/filtered') && {distance: Settings.getValue('pal/nearDistance')};
    var data = [], avatars = AvatarList.getAvatarIdentifiers();
    avatarsOfInterest = {};
    var myPosition = filter && Camera.position,
        frustum = filter && Camera.frustum,
        verticalHalfAngle = filter && (frustum.fieldOfView / 2),
        horizontalHalfAngle = filter && (verticalHalfAngle * frustum.aspectRatio),
        orientation = filter && Camera.orientation,
        front = filter && Quat.getFront(orientation),
        verticalAngleNormal = filter && Quat.getRight(orientation),
        horizontalAngleNormal = filter && Quat.getUp(orientation);
    avatars.forEach(function (id) { // sorting the identifiers is just an aid for debugging
        var avatar = AvatarList.getAvatar(id);
        var name = avatar.sessionDisplayName;
        if (!name) {
            // Either we got a data packet but no identity yet, or something is really messed up. In any case,
            // we won't be able to do anything with this user, so don't include them.
            // In normal circumstances, a refresh will bring in the new user, but if we're very heavily loaded,
            // we could be losing and gaining people randomly.
            print('No avatar identity data for', id);
            return;
        }
        if (id && myPosition && (Vec3.distance(avatar.position, myPosition) > filter.distance)) {
            return;
        }
        var normal = id && filter && Vec3.normalize(Vec3.subtract(avatar.position, myPosition));
        var horizontal = normal && angleBetweenVectorsInPlane(normal, front, horizontalAngleNormal);
        var vertical = normal && angleBetweenVectorsInPlane(normal, front, verticalAngleNormal);
        if (id && filter && ((Math.abs(horizontal) > horizontalHalfAngle) || (Math.abs(vertical) > verticalHalfAngle))) {
            return;
        }
        var avatarPalDatum = {
            displayName: name,
            userName: '',
            sessionId: id || '',
            audioLevel: 0.0,
            admin: false,
            personalMute: !!id && Users.getPersonalMuteStatus(id), // expects proper boolean, not null
            ignore: !!id && Users.getIgnoreStatus(id) // ditto
        };
        if (id) {
            addAvatarNode(id); // No overlay for ourselves
            // Everyone needs to see admin status. Username and fingerprint returns default constructor output if the requesting user isn't an admin.
            Users.requestUsernameFromID(id);
            avatarsOfInterest[id] = true;
        }
        data.push(avatarPalDatum);
        print('PAL data:', JSON.stringify(avatarPalDatum));
    });
    conserveResources = Object.keys(avatarsOfInterest).length > 20;
    sendToQml({ method: 'users', params: data });
    if (selectData) {
        selectData[2] = true;
        sendToQml({ method: 'select', params: selectData });
    }
}

// The function that handles the reply from the server
function usernameFromIDReply(id, username, machineFingerprint, isAdmin) {
    var data = [
        (MyAvatar.sessionUUID === id) ? '' : id, // Pal.qml recognizes empty id specially.
        // If we get username (e.g., if in future we receive it when we're friends), use it.
        // Otherwise, use valid machineFingerprint (which is not valid when not an admin).
        username || (Users.canKick && machineFingerprint) || '',
        isAdmin
    ];
    // Ship the data off to QML
    sendToQml({ method: 'updateUsername', params: data });
}

var pingPong = true;
function updateOverlays() {
    var eye = Camera.position;
    AvatarList.getAvatarIdentifiers().forEach(function (id) {
        if (!id || !avatarsOfInterest[id]) {
            return; // don't update ourself, or avatars we're not interested in
        }
        var avatar = AvatarList.getAvatar(id);
        if (!avatar) {
            return; // will be deleted below if there had been an overlay.
        }
        var overlay = ExtendedOverlay.get(id);
        if (!overlay) { // For now, we're treating this as a temporary loss, as from the personal space bubble. Add it back.
            print('Adding non-PAL avatar node', id);
            overlay = addAvatarNode(id);
        }
        var target = avatar.position;
        var distance = Vec3.distance(target, eye);
        var offset = 0.2;

        // base offset on 1/2 distance from hips to head if we can
        var headIndex = avatar.getJointIndex("Head");
        if (headIndex > 0) {
            offset = avatar.getAbsoluteJointTranslationInObjectFrame(headIndex).y / 2;
        }

        // get diff between target and eye (a vector pointing to the eye from avatar position)
        var diff = Vec3.subtract(target, eye);

        // move a bit in front, towards the camera
        target = Vec3.subtract(target, Vec3.multiply(Vec3.normalize(diff), offset));

        // now bump it up a bit
        target.y = target.y + offset;

        overlay.ping = pingPong;
        overlay.editOverlay({
            color: color(ExtendedOverlay.isSelected(id), overlay.hovering, overlay.audioLevel),
            position: target,
            dimensions: 0.032 * distance
        });
        if (overlay.model) {
            overlay.model.ping = pingPong;
            overlay.model.editOverlay({
                position: target,
                scale: 0.2 * distance, // constant apparent size
                rotation: Camera.orientation
            });
        }
    });
    pingPong = !pingPong;
    ExtendedOverlay.some(function (overlay) { // Remove any that weren't updated. (User is gone.)
        if (overlay.ping === pingPong) {
            overlay.deleteOverlay();
        }
    });
    // We could re-populateUserList if anything added or removed, but not for now.
    HighlightedEntity.updateOverlays();
}
function removeOverlays() {
    selectedIds = [];
    lastHoveringId = 0;
    HighlightedEntity.clearOverlays();
    ExtendedOverlay.some(function (overlay) {
        overlay.deleteOverlay();
    });
}

//
// Clicks.
//
function handleClick(pickRay) {
    ExtendedOverlay.applyPickRay(pickRay, function (overlay) {
        // Don't select directly. Tell qml, who will give us back a list of ids.
        var message = {method: 'select', params: [[overlay.key], !overlay.selected, false]};
        sendToQml(message);
        return true;
    });
}
function handleMouseEvent(mousePressEvent) { // handleClick if we get one.
    if (!mousePressEvent.isLeftButton) {
        return;
    }
    handleClick(Camera.computePickRay(mousePressEvent.x, mousePressEvent.y));
}
function handleMouseMove(pickRay) { // given the pickRay, just do the hover logic
    ExtendedOverlay.applyPickRay(pickRay, function (overlay) {
        overlay.hover(true);
    }, function () {
        ExtendedOverlay.unHover();
    });
}

// handy global to keep track of which hand is the mouse (if any)
var currentHandPressed = 0;
var TRIGGER_CLICK_THRESHOLD = 0.85;
var TRIGGER_PRESS_THRESHOLD = 0.05;

function handleMouseMoveEvent(event) { // find out which overlay (if any) is over the mouse position
    var pickRay;
    if (HMD.active) {
        if (currentHandPressed !== 0) {
            pickRay = controllerComputePickRay(currentHandPressed);
        } else {
            // nothing should hover, so
            ExtendedOverlay.unHover();
            return;
        }
    } else {
        pickRay = Camera.computePickRay(event.x, event.y);
    }
    handleMouseMove(pickRay);
}
function handleTriggerPressed(hand, value) {
    // The idea is if you press one trigger, it is the one
    // we will consider the mouse.  Even if the other is pressed,
    // we ignore it until this one is no longer pressed.
    var isPressed = value > TRIGGER_PRESS_THRESHOLD;
    if (currentHandPressed === 0) {
        currentHandPressed = isPressed ? hand : 0;
        return;
    }
    if (currentHandPressed === hand) {
        currentHandPressed = isPressed ? hand : 0;
        return;
    }
    // otherwise, the other hand is still triggered
    // so do nothing.
}

// We get mouseMoveEvents from the handControllers, via handControllerPointer.
// But we don't get mousePressEvents.
var triggerMapping = Controller.newMapping(Script.resolvePath('') + '-click');
var triggerPressMapping = Controller.newMapping(Script.resolvePath('') + '-press');
function controllerComputePickRay(hand) {
    var controllerPose = getControllerWorldLocation(hand, true);
    if (controllerPose.valid) {
        return { origin: controllerPose.position, direction: Quat.getUp(controllerPose.orientation) };
    }
}
function makeClickHandler(hand) {
    return function (clicked) {
        if (clicked > TRIGGER_CLICK_THRESHOLD) {
            var pickRay = controllerComputePickRay(hand);
            handleClick(pickRay);
        }
    };
}
function makePressHandler(hand) {
    return function (value) {
        handleTriggerPressed(hand, value);
    };
}
triggerMapping.from(Controller.Standard.RTClick).peek().to(makeClickHandler(Controller.Standard.RightHand));
triggerMapping.from(Controller.Standard.LTClick).peek().to(makeClickHandler(Controller.Standard.LeftHand));
triggerPressMapping.from(Controller.Standard.RT).peek().to(makePressHandler(Controller.Standard.RightHand));
triggerPressMapping.from(Controller.Standard.LT).peek().to(makePressHandler(Controller.Standard.LeftHand));
//
// Manage the connection between the button and the window.
//
var button;
var buttonName = "PEOPLE";
var tablet = null;

function startup() {
    tablet = Tablet.getTablet("com.highfidelity.interface.tablet.system");
    button = tablet.addButton({
        text: buttonName,
        icon: "icons/tablet-icons/people-i.svg",
        activeIcon: "icons/tablet-icons/people-a.svg",
        sortOrder: 7
    });
    tablet.fromQml.connect(fromQml);
    button.clicked.connect(onTabletButtonClicked);
    tablet.screenChanged.connect(onTabletScreenChanged);
    Users.usernameFromIDReply.connect(usernameFromIDReply);
    Window.domainChanged.connect(clearLocalQMLDataAndClosePAL);
    Window.domainConnectionRefused.connect(clearLocalQMLDataAndClosePAL);
    Messages.subscribe(CHANNEL);
    Messages.messageReceived.connect(receiveMessage);
    Users.avatarDisconnected.connect(avatarDisconnected);
}

startup();

var isWired = false;
var audioTimer;
var AUDIO_LEVEL_UPDATE_INTERVAL_MS = 100; // 10hz for now (change this and change the AVERAGING_RATIO too)
var AUDIO_LEVEL_CONSERVED_UPDATE_INTERVAL_MS = 300;
function off() {
    if (isWired) { // It is not ok to disconnect these twice, hence guard.
        Script.update.disconnect(updateOverlays);
        Controller.mousePressEvent.disconnect(handleMouseEvent);
        Controller.mouseMoveEvent.disconnect(handleMouseMoveEvent);
        isWired = false;
    }
    if (audioTimer) {
        Script.clearInterval(audioTimer);
    }
    triggerMapping.disable(); // It's ok if we disable twice.
    triggerPressMapping.disable(); // see above
    removeOverlays();
    Users.requestsDomainListData = false;
}

var onPalScreen = false;
var shouldActivateButton = false;

function onTabletButtonClicked() {
    if (onPalScreen) {
        // for toolbar-mode: go back to home screen, this will close the window.
        tablet.gotoHomeScreen();
    } else {
        shouldActivateButton = true;
        tablet.loadQMLSource("../Pal.qml");
        onPalScreen = true;
        Users.requestsDomainListData = true;
        populateUserList();
        isWired = true;
        Script.update.connect(updateOverlays);
        Controller.mousePressEvent.connect(handleMouseEvent);
        Controller.mouseMoveEvent.connect(handleMouseMoveEvent);
        triggerMapping.enable();
        triggerPressMapping.enable();
        audioTimer = createAudioInterval(conserveResources ? AUDIO_LEVEL_CONSERVED_UPDATE_INTERVAL_MS : AUDIO_LEVEL_UPDATE_INTERVAL_MS);
    }
}

function onTabletScreenChanged(type, url) {
    // for toolbar mode: change button to active when window is first openend, false otherwise.
    button.editProperties({isActive: shouldActivateButton});
    shouldActivateButton = false;
    onPalScreen = false;

    // disable sphere overlays when not on pal screen.
    if (type !== "QML" || url !== "../Pal.qml") {
        off();
    }
}

//
// Message from other scripts, such as edit.js
//
var CHANNEL = 'com.highfidelity.pal';
function receiveMessage(channel, messageString, senderID) {
    if ((channel !== CHANNEL) ||
        (senderID !== MyAvatar.sessionUUID)) {
        return;
    }
    var message = JSON.parse(messageString);
    switch (message.method) {
    case 'select':
        sendToQml(message); // Accepts objects, not just strings.
        break;
    default:
        print('Unrecognized PAL message', messageString);
    }
}


var AVERAGING_RATIO = 0.05;
var LOUDNESS_FLOOR = 11.0;
var LOUDNESS_SCALE = 2.8 / 5.0;
var LOG2 = Math.log(2.0);
var myData = {}; // we're not includied in ExtendedOverlay.get.

function getAudioLevel(id) {
    // the VU meter should work similarly to the one in AvatarInputs: log scale, exponentially averaged
    // But of course it gets the data at a different rate, so we tweak the averaging ratio and frequency
    // of updating (the latter for efficiency too).
    var avatar = AvatarList.getAvatar(id);
    var audioLevel = 0.0;
    var data = id ? ExtendedOverlay.get(id) : myData;
    if (!data) {
        return audioLevel;
    }

    // we will do exponential moving average by taking some the last loudness and averaging
    data.accumulatedLevel = AVERAGING_RATIO * (data.accumulatedLevel || 0) + (1 - AVERAGING_RATIO) * (avatar.audioLoudness);

    // add 1 to insure we don't go log() and hit -infinity.  Math.log is
    // natural log, so to get log base 2, just divide by ln(2).
    var logLevel = Math.log(data.accumulatedLevel + 1) / LOG2;

    if (logLevel <= LOUDNESS_FLOOR) {
        audioLevel = logLevel / LOUDNESS_FLOOR * LOUDNESS_SCALE;
    } else {
        audioLevel = (logLevel - (LOUDNESS_FLOOR - 1.0)) * LOUDNESS_SCALE;
    }
    if (audioLevel > 1.0) {
        audioLevel = 1;
    }
    data.audioLevel = audioLevel;
    return audioLevel;
}

function createAudioInterval(interval) {
    // we will update the audioLevels periodically
    // TODO: tune for efficiency - expecially with large numbers of avatars
    return Script.setInterval(function () {
        var param = {};
        AvatarList.getAvatarIdentifiers().forEach(function (id) {
            var level = getAudioLevel(id);
            // qml didn't like an object with null/empty string for a key, so...
            var userId = id || 0;
            param[userId] = level;
        });
        sendToQml({method: 'updateAudioLevel', params: param});
    }, interval);
}

function avatarDisconnected(nodeID) {
    // remove from the pal list
    sendToQml({method: 'avatarDisconnected', params: [nodeID]});
}

function clearLocalQMLDataAndClosePAL() {
    sendToQml({ method: 'clearLocalQMLData' });
}

function shutdown() {
    button.clicked.disconnect(onTabletButtonClicked);
    tablet.removeButton(button);
    tablet.screenChanged.disconnect(onTabletScreenChanged);
    Users.usernameFromIDReply.disconnect(usernameFromIDReply);
    Window.domainChanged.disconnect(clearLocalQMLDataAndClosePAL);
    Window.domainConnectionRefused.disconnect(clearLocalQMLDataAndClosePAL);
    Messages.subscribe(CHANNEL);
    Messages.messageReceived.disconnect(receiveMessage);
    Users.avatarDisconnected.disconnect(avatarDisconnected);
    off();
}

//
// Cleanup.
//
Script.scriptEnding.connect(shutdown);

}()); // END LOCAL_SCOPE
