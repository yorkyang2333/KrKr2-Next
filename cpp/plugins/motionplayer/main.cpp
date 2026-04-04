//
// Created by LiDon on 2025/9/13.
// TODO: implement emoteplayer.dll plugin
//
#include <spdlog/spdlog.h>
#include "tjs.h"
#include "tjsDictionary.h"
#include "ncbind.hpp"
#include "psbfile/PSBFile.h"

#include "ResourceManager.h"
#include "EmotePlayer.h"
#include "Player.h"
#include "SeparateLayerAdaptor.h"

using namespace motion;
using namespace TJS;

#define NCB_MODULE_NAME TJS_W("motionplayer.dll")
#define LOGGER spdlog::get("plugin")

// Forward declarations for stub functions
static tjs_error D3DAdaptorMissingHandler(tTJSVariant *r, tjs_int count, tTJSVariant **p, iTJSDispatch2 *objthis);
static tjs_error D3DAdaptorStub_noop(tTJSVariant *, tjs_int, tTJSVariant **, iTJSDispatch2 *);

static tjs_int SafeAsInteger(tTJSVariant *v) {
    if (!v) return 0;
    if (v->Type() == tvtObject) return 0;
    try { return static_cast<tjs_int>(v->AsInteger()); } catch(...) { return 0; }
}

static tjs_real SafeAsReal(tTJSVariant *v) {
    if (!v) return 0.0;
    if (v->Type() == tvtObject) return 0.0;
    try { return static_cast<tjs_real>(v->AsReal()); } catch(...) { return 0.0; }
}

extern void TVPAddLog(const ttstr &line);

static tjs_error D3DAdaptorMissingHandler(tTJSVariant *r, tjs_int count, tTJSVariant **p, iTJSDispatch2 *objthis) {
    if (count < 2) return TJS_E_INVALIDPARAM;
    bool isSet = SafeAsInteger(p[0]) != 0;
    
    ttstr memberName = *p[1];
    TVPAddLog(TJS_W("KrKr2-Next Missing property accessed: ") + memberName);
    if (auto l = LOGGER) {
        l->error("Missing member accessed: {}", memberName.AsStdString());
        l->flush();
    }
    
    // Hardcode some known primitive properties to 0 to prevent to-int cast crashes
    if (memberName == TJS_W("clearColor") || memberName == TJS_W("bgColor") || memberName == TJS_W("neutralColor") || 
        memberName == TJS_W("opacity") || memberName == TJS_W("alpha") ||
        memberName == TJS_W("width") || memberName == TJS_W("height") ||
        memberName == TJS_W("clearEnabled") || memberName == TJS_W("canvasCaptureEnabled")) {
        if (r) *r = tTJSVariant((tjs_int)0);
        return TJS_S_OK;
    }

    if (!isSet && r) {
        // If it starts with 'on', it's likely an event handler callback, so return NoOp
        if (memberName.StartsWith(TJS_W("on"))) {
            class NoOpFunc : public tTJSDispatch {
            public:
                tjs_error FuncCall(tjs_uint32, const tjs_char *, tjs_uint32 *, tTJSVariant *res, tjs_int, tTJSVariant **, iTJSDispatch2 *) override {
                    if (res) res->Clear();
                    return TJS_S_OK;
                }
            };
            auto *func = new NoOpFunc();
            *r = tTJSVariant(func, func);
            func->Release();
        } else {
            // For all other unknown properties, return 0 to satisfy numeric evaluations
            *r = tTJSVariant((tjs_int)0);
        }
    }
    return TJS_S_OK;
}

static tjs_error D3DAdaptorStub_noop(tTJSVariant *, tjs_int, tTJSVariant **, iTJSDispatch2 *) { return TJS_S_OK; }

static motion::SeparateLayerAdaptor *GetSeparateLayerAdaptorInstance(iTJSDispatch2 *objthis) {
    return ncbInstanceAdaptor<motion::SeparateLayerAdaptor>::GetNativeInstance(objthis);
}

static iTJSDispatch2 *GetSeparateAdaptorRenderTarget(motion::SeparateLayerAdaptor *adaptor);

iTJSDispatch2 *ResolveLayerTreeOwnerBase(iTJSDispatch2 *base) {
    if(!base) return nullptr;
    auto *adaptor = ncbInstanceAdaptor<motion::SeparateLayerAdaptor>::GetNativeInstance(base);
    if(adaptor) return GetSeparateAdaptorRenderTarget(adaptor);
    return base;
}

static iTJSDispatch2 *GetSeparateAdaptorRenderTarget(motion::SeparateLayerAdaptor *adaptor) {
    if(!adaptor) return nullptr;
    if(adaptor->getTarget()) return adaptor->getTarget();

    auto *owner = adaptor->getOwner();
    if(!owner) return nullptr;

    tTJSVariant windowVar;
    iTJSDispatch2 *windowObj = owner;
    if(TJS_SUCCEEDED(owner->PropGet(0, TJS_W("window"), nullptr, &windowVar, owner)) &&
       windowVar.Type() == tvtObject && windowVar.AsObjectNoAddRef()) {
        windowObj = windowVar.AsObjectNoAddRef();
    }

    tTJSVariant parentVar;
    if(TJS_FAILED(owner->PropGet(0, TJS_W("primaryLayer"), nullptr, &parentVar, owner)) ||
       parentVar.Type() != tvtObject || !parentVar.AsObjectNoAddRef()) {
        if(TJS_FAILED(windowObj->PropGet(0, TJS_W("primaryLayer"), nullptr, &parentVar, windowObj)) ||
           parentVar.Type() != tvtObject || !parentVar.AsObjectNoAddRef()) {
            return owner;
        }
    }

    iTJSDispatch2 *global = TVPGetScriptDispatch();
    if(!global) return owner;

    tTJSVariant layerClassVar;
    if(TJS_FAILED(global->PropGet(0, TJS_W("Layer"), nullptr, &layerClassVar, global)) ||
       layerClassVar.Type() != tvtObject || !layerClassVar.AsObjectNoAddRef()) {
        global->Release();
        return owner;
    }

    iTJSDispatch2 *layerClass = layerClassVar.AsObjectNoAddRef();
    tTJSVariant args[2] = { tTJSVariant(windowObj, windowObj),
                            tTJSVariant(parentVar.AsObjectNoAddRef(), parentVar.AsObjectNoAddRef()) };
    tTJSVariant *argv[] = { &args[0], &args[1] };
    iTJSDispatch2 *layerObj = nullptr;
    const auto hr = layerClass->CreateNew(0, nullptr, nullptr, &layerObj, 2, argv, layerClass);
    global->Release();
    if(TJS_FAILED(hr) || !layerObj) {
        return owner;
    }

    auto syncProp = [&](const tjs_char *name) {
        tTJSVariant value;
        if(TJS_SUCCEEDED(owner->PropGet(0, name, nullptr, &value, owner))) {
            layerObj->PropSet(TJS_MEMBERENSURE, name, nullptr, &value, layerObj);
        }
    };
    syncProp(TJS_W("left"));
    syncProp(TJS_W("top"));
    syncProp(TJS_W("width"));
    syncProp(TJS_W("height"));
    syncProp(TJS_W("visible"));
    syncProp(TJS_W("opacity"));
    syncProp(TJS_W("name"));

    // Prevent the render target from intercepting mouse events;
    // hitThreshold=256 makes hit test always fail (max alpha is 255)
    tTJSVariant htVal(static_cast<tjs_int>(256));
    layerObj->PropSet(TJS_MEMBERENSURE, TJS_W("hitThreshold"), nullptr, &htVal, layerObj);

    // Ensure the owner AnimKAGLayer passes hit test even without its own bitmap;
    // hitThreshold=0 means bounds-only checking (no alpha test needed)
    tTJSVariant ownerHtVal(static_cast<tjs_int>(0));
    owner->PropSet(TJS_MEMBERENSURE, TJS_W("hitThreshold"), nullptr, &ownerHtVal, owner);

    adaptor->setTarget(layerObj);
    layerObj->Release();
    return adaptor->getTarget() ? adaptor->getTarget() : owner;
}

static tjs_error SeparateLayerAdaptor_getWidth(tTJSVariant *r, tjs_int, tTJSVariant **,
                                               iTJSDispatch2 *objthis) {
    auto *adaptor = GetSeparateLayerAdaptorInstance(objthis);
    auto *target = GetSeparateAdaptorRenderTarget(adaptor);
    if(!target) {
        if(r) *r = tTJSVariant(static_cast<tjs_int>(0));
        return TJS_S_OK;
    }
    tTJSVariant value;
    const auto hr = target->PropGet(0, TJS_W("width"), nullptr, &value, target);
    if(r) {
        *r = TJS_SUCCEEDED(hr) ? value : tTJSVariant(static_cast<tjs_int>(0));
    }
    return TJS_S_OK;
}

static tjs_error SeparateLayerAdaptor_getHeight(tTJSVariant *r, tjs_int, tTJSVariant **,
                                                iTJSDispatch2 *objthis) {
    auto *adaptor = GetSeparateLayerAdaptorInstance(objthis);
    auto *target = GetSeparateAdaptorRenderTarget(adaptor);
    if(!target) {
        if(r) *r = tTJSVariant(static_cast<tjs_int>(0));
        return TJS_S_OK;
    }
    tTJSVariant value;
    const auto hr = target->PropGet(0, TJS_W("height"), nullptr, &value, target);
    if(r) {
        *r = TJS_SUCCEEDED(hr) ? value : tTJSVariant(static_cast<tjs_int>(0));
    }
    return TJS_S_OK;
}

static tjs_error SeparateLayerAdaptor_loadImages(tTJSVariant *r, tjs_int count, tTJSVariant **p,
                                                 iTJSDispatch2 *objthis) {
    auto *adaptor = GetSeparateLayerAdaptorInstance(objthis);
    auto *target = GetSeparateAdaptorRenderTarget(adaptor);
    if(!target || count < 1) return TJS_E_INVALIDPARAM;
    return target->FuncCall(0, TJS_W("loadImages"), nullptr, r, count, p, target);
}

static tjs_error SeparateLayerAdaptor_fillRect(tTJSVariant *r, tjs_int count, tTJSVariant **p,
                                               iTJSDispatch2 *objthis) {
    auto *adaptor = GetSeparateLayerAdaptorInstance(objthis);
    auto *target = GetSeparateAdaptorRenderTarget(adaptor);
    if(!target || count < 5) return TJS_E_INVALIDPARAM;
    return target->FuncCall(0, TJS_W("fillRect"), nullptr, r, count, p, target);
}

static tjs_error SeparateLayerAdaptor_operateRect(tTJSVariant *r, tjs_int count, tTJSVariant **p,
                                                  iTJSDispatch2 *objthis) {
    auto *adaptor = GetSeparateLayerAdaptorInstance(objthis);
    auto *target = GetSeparateAdaptorRenderTarget(adaptor);
    if(!target || count < 9) return TJS_E_INVALIDPARAM;
    return target->FuncCall(0, TJS_W("operateRect"), nullptr, r, count, p, target);
}

static tjs_error SeparateLayerAdaptor_getFace(tTJSVariant *r, tjs_int, tTJSVariant **,
                                              iTJSDispatch2 *objthis) {
    auto *adaptor = GetSeparateLayerAdaptorInstance(objthis);
    auto *target = GetSeparateAdaptorRenderTarget(adaptor);
    if(!target) { if(r) *r = tTJSVariant(static_cast<tjs_int>(0)); return TJS_S_OK; }
    tTJSVariant value;
    const auto hr = target->PropGet(0, TJS_W("face"), nullptr, &value, target);
    if(r) *r = TJS_SUCCEEDED(hr) ? value : tTJSVariant(static_cast<tjs_int>(0));
    return TJS_S_OK;
}

static tjs_error SeparateLayerAdaptor_setFace(tTJSVariant *r, tjs_int count, tTJSVariant **p,
                                              iTJSDispatch2 *objthis) {
    auto *adaptor = GetSeparateLayerAdaptorInstance(objthis);
    auto *target = GetSeparateAdaptorRenderTarget(adaptor);
    if(!target || count < 1) return TJS_E_INVALIDPARAM;
    return target->PropSet(TJS_MEMBERENSURE, TJS_W("face"), nullptr, p[0], target);
}

static tjs_error SeparateLayerAdaptor_getImageWidth(tTJSVariant *r, tjs_int, tTJSVariant **,
                                                    iTJSDispatch2 *objthis) {
    auto *adaptor = GetSeparateLayerAdaptorInstance(objthis);
    auto *target = GetSeparateAdaptorRenderTarget(adaptor);
    if(!target) { if(r) *r = tTJSVariant(static_cast<tjs_int>(0)); return TJS_S_OK; }
    tTJSVariant value;
    const auto hr = target->PropGet(0, TJS_W("imageWidth"), nullptr, &value, target);
    if(r) *r = TJS_SUCCEEDED(hr) ? value : tTJSVariant(static_cast<tjs_int>(0));
    return TJS_S_OK;
}

static tjs_error SeparateLayerAdaptor_getImageHeight(tTJSVariant *r, tjs_int, tTJSVariant **,
                                                     iTJSDispatch2 *objthis) {
    auto *adaptor = GetSeparateLayerAdaptorInstance(objthis);
    auto *target = GetSeparateAdaptorRenderTarget(adaptor);
    if(!target) { if(r) *r = tTJSVariant(static_cast<tjs_int>(0)); return TJS_S_OK; }
    tTJSVariant value;
    const auto hr = target->PropGet(0, TJS_W("imageHeight"), nullptr, &value, target);
    if(r) *r = TJS_SUCCEEDED(hr) ? value : tTJSVariant(static_cast<tjs_int>(0));
    return TJS_S_OK;
}

NCB_REGISTER_SUBCLASS_DELAY(SeparateLayerAdaptor) {
    NCB_CONSTRUCTOR((iTJSDispatch2 *));
    NCB_PROPERTY_RAW_CALLBACK_RO(width, SeparateLayerAdaptor_getWidth, 0);
    NCB_PROPERTY_RAW_CALLBACK_RO(height, SeparateLayerAdaptor_getHeight, 0);
    NCB_PROPERTY_RAW_CALLBACK(face, SeparateLayerAdaptor_getFace, SeparateLayerAdaptor_setFace, 0);
    NCB_PROPERTY_RAW_CALLBACK_RO(imageWidth, SeparateLayerAdaptor_getImageWidth, 0);
    NCB_PROPERTY_RAW_CALLBACK_RO(imageHeight, SeparateLayerAdaptor_getImageHeight, 0);
    NCB_METHOD_RAW_CALLBACK(loadImages, SeparateLayerAdaptor_loadImages, 0);
    NCB_METHOD_RAW_CALLBACK(fillRect, SeparateLayerAdaptor_fillRect, 0);
    NCB_METHOD_RAW_CALLBACK(operateRect, SeparateLayerAdaptor_operateRect, 0);
}

// 脚本意图: EmoteVariable.useD3D = (typeof Motion.Player.useD3D === "Object") ? Motion.Player.useD3D : Motion.enableD3D;
// 但 then 分支实际赋的是比较结果 (int)1 而非对象，导致 (int)1 to Object。故让 useD3D 返回整数，
// 使 typeof === "Integer" 走 else，赋 Motion.enableD3D（stub 对象），避免两处 int→Object 报错。
static tjs_error Player_getUseD3D(tTJSVariant *r, tjs_int, tTJSVariant **, iTJSDispatch2 *) {
    *r = tTJSVariant(static_cast<tjs_int>(0));
    return TJS_S_OK;
}
static tjs_error Player_setUseD3D(tTJSVariant *, tjs_int count, tTJSVariant **p, iTJSDispatch2 *) {
    if (count >= 1 && (*p)->Type() == tvtInteger)
        motion::Player::setUseD3D(static_cast<bool>(**p));
    return TJS_S_OK;
}

// Player_getTags: returns an empty TJS Array for the Windows-only "tags" property.
// Game script accesses player.tags.count, which requires a proper Array object.
static tjs_error Player_getTags(tTJSVariant *r, tjs_int, tTJSVariant **, iTJSDispatch2 *) {
    if (r) {
        iTJSDispatch2 *arr = TJSCreateArrayObject();
        if (arr) { *r = tTJSVariant(arr, arr); arr->Release(); }
        else r->Clear();
    }
    return TJS_S_OK;
}

// Player_getZero: returns (int)0 for properties that are expected to be scalar values
// rather than callable functions. (e.g. times, boolean flags).
static tjs_error Player_getZero(tTJSVariant *r, tjs_int, tTJSVariant **, iTJSDispatch2 *) {
    if (r) *r = tTJSVariant((tjs_int)0);
    return TJS_S_OK;
}

static tjs_error Player_getEnableD3D(tTJSVariant *r, tjs_int, tTJSVariant **, iTJSDispatch2 *) {
    iTJSDispatch2 *obj = TJSCreateDictionaryObject();
    if (obj) {
        *r = tTJSVariant(obj);
        obj->Release();
    } else {
        *r = tTJSVariant();
    }
    return TJS_S_OK;
}
static tjs_error Player_setEnableD3D(tTJSVariant *, tjs_int count, tTJSVariant **p, iTJSDispatch2 *) {
    if (count >= 1 && (*p)->Type() == tvtInteger)
        motion::Player::setEnableD3D(static_cast<bool>(**p));
    return TJS_S_OK;
}

static tjs_error Player_setVariable(tTJSVariant *r, tjs_int count, tTJSVariant **p,
                                    iTJSDispatch2 *objthis) {
    auto *player = ncbInstanceAdaptor<motion::Player>::GetNativeInstance(objthis);
    if(!player || count < 2) return TJS_E_INVALIDPARAM;
    player->setVariable(ttstr(*p[0]), *p[1]);
    if(r) *r = tTJSVariant();
    return TJS_S_OK;
}

static tjs_error Player_getVariable(tTJSVariant *r, tjs_int count, tTJSVariant **p,
                                    iTJSDispatch2 *objthis) {
    auto *player = ncbInstanceAdaptor<motion::Player>::GetNativeInstance(objthis);
    if(!player || count < 1) return TJS_E_INVALIDPARAM;
    if(r) *r = player->getVariable(ttstr(*p[0]));
    return TJS_S_OK;
}

static motion::Player *GetPlayerInstance(iTJSDispatch2 *objthis) {
    return ncbInstanceAdaptor<motion::Player>::GetNativeInstance(objthis);
}

static tjs_error Player_getPlaying(tTJSVariant *r, tjs_int, tTJSVariant **,
                                   iTJSDispatch2 *objthis) {
    auto *player = GetPlayerInstance(objthis);
    if(r) *r = tTJSVariant(player ? player->getPlaying() : false);
    return TJS_S_OK;
}

static tjs_error Player_getAllplaying(tTJSVariant *r, tjs_int, tTJSVariant **,
                                      iTJSDispatch2 *objthis) {
    auto *player = GetPlayerInstance(objthis);
    if(r) *r = tTJSVariant(player ? player->getAllplaying() : false);
    return TJS_S_OK;
}

static tjs_error Player_getMotion(tTJSVariant *r, tjs_int, tTJSVariant **,
                                  iTJSDispatch2 *objthis) {
    auto *player = GetPlayerInstance(objthis);
    if(r) *r = tTJSVariant(player ? player->getMotion() : ttstr());
    return TJS_S_OK;
}

static tjs_error Player_setMotion(tTJSVariant *, tjs_int count, tTJSVariant **p,
                                  iTJSDispatch2 *objthis) {
    auto *player = GetPlayerInstance(objthis);
    if(!player || count < 1) return TJS_E_INVALIDPARAM;
    player->setMotion(ttstr(*p[0]));
    return TJS_S_OK;
}

static tjs_error Player_getChara(tTJSVariant *r, tjs_int, tTJSVariant **,
                                 iTJSDispatch2 *objthis) {
    auto *player = GetPlayerInstance(objthis);
    if(r) *r = tTJSVariant(player ? player->getChara() : ttstr());
    return TJS_S_OK;
}

static tjs_error Player_setChara(tTJSVariant *, tjs_int count, tTJSVariant **p,
                                 iTJSDispatch2 *objthis) {
    auto *player = GetPlayerInstance(objthis);
    if(!player || count < 1) return TJS_E_INVALIDPARAM;
    player->setChara(ttstr(*p[0]));
    return TJS_S_OK;
}

static tjs_error Player_getTickCount(tTJSVariant *r, tjs_int, tTJSVariant **,
                                     iTJSDispatch2 *objthis) {
    auto *player = GetPlayerInstance(objthis);
    if(r) *r = tTJSVariant(static_cast<tjs_int>(player ? player->getTickCount() : 0));
    return TJS_S_OK;
}

static tjs_error Player_setTickCount(tTJSVariant *, tjs_int count, tTJSVariant **p,
                                     iTJSDispatch2 *objthis) {
    auto *player = GetPlayerInstance(objthis);
    if(!player || count < 1) return TJS_E_INVALIDPARAM;
    player->setTickCount(static_cast<tjs_int>(**p));
    return TJS_S_OK;
}

static tjs_error Player_getLastTime(tTJSVariant *r, tjs_int, tTJSVariant **,
                                    iTJSDispatch2 *objthis) {
    auto *player = GetPlayerInstance(objthis);
    if(r) *r = tTJSVariant(static_cast<tjs_int>(player ? player->getLastTime() : 0));
    return TJS_S_OK;
}

static tjs_error Player_setLastTime(tTJSVariant *, tjs_int count, tTJSVariant **p,
                                    iTJSDispatch2 *objthis) {
    auto *player = GetPlayerInstance(objthis);
    if(!player || count < 1) return TJS_E_INVALIDPARAM;
    player->setLastTime(static_cast<tjs_int>(**p));
    return TJS_S_OK;
}

static tjs_error Player_getSpeed(tTJSVariant *r, tjs_int, tTJSVariant **,
                                 iTJSDispatch2 *objthis) {
    auto *player = GetPlayerInstance(objthis);
    if(r) *r = tTJSVariant(static_cast<tjs_real>(player ? player->getSpeed() : 1.0));
    return TJS_S_OK;
}

static tjs_error Player_setSpeed(tTJSVariant *, tjs_int count, tTJSVariant **p,
                                 iTJSDispatch2 *objthis) {
    auto *player = GetPlayerInstance(objthis);
    if(!player || count < 1) return TJS_E_INVALIDPARAM;
    player->setSpeed(SafeAsReal(p[0]));
    return TJS_S_OK;
}

static tjs_error Player_getCompletionType(tTJSVariant *r, tjs_int, tTJSVariant **,
                                          iTJSDispatch2 *objthis) {
    auto *player = GetPlayerInstance(objthis);
    if(r) *r = tTJSVariant(static_cast<tjs_int>(player ? player->getCompletionType() : 0));
    return TJS_S_OK;
}

static tjs_error Player_setCompletionType(tTJSVariant *, tjs_int count, tTJSVariant **p,
                                          iTJSDispatch2 *objthis) {
    auto *player = GetPlayerInstance(objthis);
    if(!player || count < 1) return TJS_E_INVALIDPARAM;
    player->setCompletionType(static_cast<tjs_int>(**p));
    return TJS_S_OK;
}

static tjs_error Player_play(tTJSVariant *, tjs_int count, tTJSVariant **p,
                             iTJSDispatch2 *objthis) {
    auto *player = GetPlayerInstance(objthis);
    if(!player || count < 1) return TJS_E_INVALIDPARAM;
    const ttstr motion = ttstr(*p[0]);
    const tjs_int all = count >= 2 ? static_cast<tjs_int>(p[1]->AsInteger()) : 0;
    player->play(motion, all);
    return TJS_S_OK;
}

static tjs_error Player_stop(tTJSVariant *, tjs_int, tTJSVariant **,
                             iTJSDispatch2 *objthis) {
    auto *player = GetPlayerInstance(objthis);
    if(!player) return TJS_E_INVALIDPARAM;
    player->stop();
    return TJS_S_OK;
}

static tjs_error Player_progress(tTJSVariant *, tjs_int count, tTJSVariant **p,
                                 iTJSDispatch2 *objthis) {
    auto *player = GetPlayerInstance(objthis);
    if(!player || count < 1) return TJS_E_INVALIDPARAM;
    player->progress(SafeAsInteger(p[0]));
    return TJS_S_OK;
}

static tjs_error Player_skipToSync(tTJSVariant *, tjs_int, tTJSVariant **,
                                   iTJSDispatch2 *objthis) {
    auto *player = GetPlayerInstance(objthis);
    if(!player) return TJS_E_INVALIDPARAM;
    player->skipToSync();
    return TJS_S_OK;
}

static tjs_error Player_setDrawAffineTranslateMatrix(tTJSVariant *, tjs_int count, tTJSVariant **p,
                                                     iTJSDispatch2 *objthis) {
    auto *player = GetPlayerInstance(objthis);
    if(!player || count < 6) return TJS_E_INVALIDPARAM;
    player->setDrawAffineTranslateMatrix(
        SafeAsReal(p[0]), SafeAsReal(p[1]), SafeAsReal(p[2]),
        SafeAsReal(p[3]), SafeAsReal(p[4]), SafeAsReal(p[5]));
    return TJS_S_OK;
}

static tjs_error Player_setCoord(tTJSVariant *, tjs_int count, tTJSVariant **p,
                                 iTJSDispatch2 *objthis) {
    auto *player = GetPlayerInstance(objthis);
    if(!player || count < 2) return TJS_E_INVALIDPARAM;
    player->setCoord(SafeAsReal(p[0]), SafeAsReal(p[1]));
    return TJS_S_OK;
}

static tjs_error Player_contains(tTJSVariant *r, tjs_int count, tTJSVariant **p,
                                 iTJSDispatch2 *objthis) {
    auto *player = GetPlayerInstance(objthis);
    if(!player || count < 2) return TJS_E_INVALIDPARAM;
    if(r) *r = tTJSVariant(player->contains(SafeAsInteger(p[0]), SafeAsInteger(p[1])));
    return TJS_S_OK;
}

static tjs_error Player_getCommandList(tTJSVariant *r, tjs_int, tTJSVariant **,
                                       iTJSDispatch2 *objthis) {
    auto *player = GetPlayerInstance(objthis);
    if(!player) return TJS_E_INVALIDPARAM;
    iTJSDispatch2 *obj = player->getCommandList();
    if(r) {
        if(obj) {
            *r = tTJSVariant(obj);
            obj->Release();
        } else {
            *r = tTJSVariant();
        }
    } else if(obj) {
        obj->Release();
    }
    return TJS_S_OK;
}

static tjs_error Player_getLayerMotion(tTJSVariant *r, tjs_int count, tTJSVariant **,
                                       iTJSDispatch2 *objthis) {
    if(count < 1) return TJS_E_INVALIDPARAM;
    if(r) *r = tTJSVariant(objthis);
    return TJS_S_OK;
}

static tjs_error Player_getLayerGetter(tTJSVariant *r, tjs_int count, tTJSVariant **p,
                                       iTJSDispatch2 *objthis) {
    auto *player = GetPlayerInstance(objthis);
    if(!player || count < 1) return TJS_E_INVALIDPARAM;
    ttstr name = ttstr(*p[0]);
    iTJSDispatch2 *obj = player->createLayerGetter(objthis, name);
    if(r) {
        if(obj) {
            *r = tTJSVariant(obj);
            obj->Release();
        } else {
            *r = tTJSVariant();
        }
    } else if(obj) {
        obj->Release();
    }
    return TJS_S_OK;
}

static tjs_error Player_clear(tTJSVariant *, tjs_int count, tTJSVariant **p,
                              iTJSDispatch2 *objthis) {
    static int sClearCount = 0;
    sClearCount++;
    auto *player = GetPlayerInstance(objthis);
    if(!player || count < 2) return TJS_E_INVALIDPARAM;
    if(sClearCount <= 5 || sClearCount % 300 == 0) {
        if(auto l = LOGGER) l->info("Player_clear: callCount={}", sClearCount);
    }
    player->clear(p[0]->AsObjectNoAddRef(), SafeAsInteger(p[1]));
    return TJS_S_OK;
}

static tjs_error Player_draw(tTJSVariant *, tjs_int count, tTJSVariant **p,
                             iTJSDispatch2 *objthis) {
    static int sDrawWrapCount = 0;
    sDrawWrapCount++;
    auto *player = GetPlayerInstance(objthis);
    if(!player || count < 1) {
        if(auto l = LOGGER) l->warn("Player_draw: player={} count={} callCount={}", (void*)player, count, sDrawWrapCount);
        return TJS_E_INVALIDPARAM;
    }
    if(sDrawWrapCount <= 5 || sDrawWrapCount % 300 == 0) {
        if(auto l = LOGGER) l->info("Player_draw: calling draw, target={} callCount={}", (void*)p[0]->AsObjectNoAddRef(), sDrawWrapCount);
    }
    player->draw(p[0]->AsObjectNoAddRef());
    return TJS_S_OK;
}

static tjs_error Player_Factory(
        motion::Player **result, tjs_int /*numparams*/, tTJSVariant ** /*param*/,
        iTJSDispatch2 *objthis) {

    static bool s_dumped = false;
    if (!s_dumped) {
        s_dumped = true;
        try {
            extern iTJSTextReadStream *TVPCreateTextStreamForRead(const ttstr &name, const ttstr &mode);
            iTJSTextReadStream *stream = TVPCreateTextStreamForRead(TJS_W("affinesourcemotion.tjs"), TJS_W(""));
            if (stream) {
                ttstr content;
                stream->Read(content, 999999);
                delete stream;
                
                std::string utf8_content = content.AsNarrowStdString();
                FILE *f = fopen("/tmp/affinesourcemotion.tjs", "wb");
                if (f) {
                    fwrite(utf8_content.c_str(), 1, utf8_content.size(), f);
                    fclose(f);
                }
            }
        } catch (...) {}
    }

    *result = new motion::Player();
    if (objthis) {
        tTJSVariant m(TJS_W("missing"));
        objthis->ClassInstanceInfo(TJS_CII_SET_MISSING, 0, &m);
    }
    return TJS_S_OK;
}

NCB_REGISTER_SUBCLASS_DELAY(Player) {
    RawCallback(Player_Factory);
    NCB_PROPERTY_RAW_CALLBACK(useD3D, Player_getUseD3D, Player_setUseD3D, TJS_STATICMEMBER);
    NCB_PROPERTY_RAW_CALLBACK(enableD3D, Player_getEnableD3D, Player_setEnableD3D, TJS_STATICMEMBER);
    NCB_PROPERTY_RAW_CALLBACK_RO(playing, Player_getPlaying, 0);
    NCB_PROPERTY_RAW_CALLBACK_RO(allplaying, Player_getAllplaying, 0);
    NCB_PROPERTY_RAW_CALLBACK(motion, Player_getMotion, Player_setMotion, 0);
    NCB_PROPERTY_RAW_CALLBACK(chara, Player_getChara, Player_setChara, 0);
    NCB_PROPERTY_RAW_CALLBACK(tickCount, Player_getTickCount, Player_setTickCount, 0);
    NCB_PROPERTY_RAW_CALLBACK(lastTime, Player_getLastTime, Player_setLastTime, 0);
    NCB_PROPERTY_RAW_CALLBACK(speed, Player_getSpeed, Player_setSpeed, 0);
    NCB_PROPERTY_RAW_CALLBACK(completionType, Player_getCompletionType, Player_setCompletionType, 0);
    NCB_METHOD_RAW_CALLBACK(play, Player_play, 0);
    NCB_METHOD_RAW_CALLBACK(stop, Player_stop, 0);
    NCB_METHOD_RAW_CALLBACK(progress, Player_progress, 0);
    NCB_METHOD_RAW_CALLBACK(skipToSync, Player_skipToSync, 0);
    NCB_METHOD_RAW_CALLBACK(setDrawAffineTranslateMatrix, Player_setDrawAffineTranslateMatrix, 0);
    NCB_METHOD_RAW_CALLBACK(setCoord, Player_setCoord, 0);
    NCB_METHOD_RAW_CALLBACK(contains, Player_contains, 0);
    NCB_METHOD_RAW_CALLBACK(getCommandList, Player_getCommandList, 0);
    NCB_METHOD_RAW_CALLBACK(getLayerMotion, Player_getLayerMotion, 0);
    NCB_METHOD_RAW_CALLBACK(getLayerGetter, Player_getLayerGetter, 0);
    NCB_METHOD_RAW_CALLBACK(clear, Player_clear, 0);
    NCB_METHOD_RAW_CALLBACK(draw, Player_draw, 0);
    NCB_METHOD_RAW_CALLBACK(setVariable, Player_setVariable, 0);
    NCB_METHOD_RAW_CALLBACK(getVariable, Player_getVariable, 0);
    // Windows-only properties that return structured data (need explicit stubs):
    // "tags" is accessed as player.tags.count — must return Array (not noop func).
    NCB_PROPERTY_RAW_CALLBACK_RO(tags, Player_getTags, 0);
    // These properties are used in math/boolean expressions, so returning a function object
    // via missing() throws a type conversion error. They must return a scalar (0/false).
    NCB_PROPERTY_RAW_CALLBACK_RO(loopTime, Player_getZero, 0);
    NCB_PROPERTY_RAW_CALLBACK_RO(isPlayingMainTimeline, Player_getZero, 0);
    NCB_PROPERTY_RAW_CALLBACK_RO(animating, Player_getZero, 0);
    // Catch-all: any remaining Windows-only member silently handled via TJS "missing".
    NCB_METHOD_RAW_CALLBACK(missing, D3DAdaptorMissingHandler, 0);
    NCB_METHOD_RAW_CALLBACK(finalize, D3DAdaptorStub_noop, 0);
}

static tjs_error EmotePlayer_Factory(
        motion::EmotePlayer **result, tjs_int numparams, tTJSVariant **param,
        iTJSDispatch2 *objthis) {
    motion::ResourceManager *rm = nullptr;
    if (numparams >= 1 && param && param[0]->Type() == tvtObject) {
        rm = ncbInstanceAdaptor<motion::ResourceManager>::GetNativeInstance(
                param[0]->AsObjectNoAddRef());
    }
    *result = new motion::EmotePlayer(rm ? *rm : motion::ResourceManager());
    if (objthis) {
        tTJSVariant m(TJS_W("missing"));
        objthis->ClassInstanceInfo(TJS_CII_SET_MISSING, 0, &m);
    }
    return TJS_S_OK;
}

NCB_REGISTER_SUBCLASS_DELAY(EmotePlayer) {
    RawCallback(EmotePlayer_Factory);
    NCB_PROPERTY(useD3D, getUseD3D, setUseD3D);
    NCB_METHOD_RAW_CALLBACK(missing, D3DAdaptorMissingHandler, 0);
    NCB_METHOD_RAW_CALLBACK(finalize, D3DAdaptorStub_noop, 0);
}

static tjs_error ResourceManager_unload(tTJSVariant *, tjs_int count, tTJSVariant **p,
                                        iTJSDispatch2 *objthis) {
    auto *manager = ncbInstanceAdaptor<motion::ResourceManager>::GetNativeInstance(objthis);
    if(!manager || count < 1) return TJS_E_INVALIDPARAM;
    manager->unload(ttstr(*p[0]));
    return TJS_S_OK;
}

static tjs_error ResourceManager_clearCache(tTJSVariant *, tjs_int, tTJSVariant **,
                                            iTJSDispatch2 *objthis) {
    auto *manager = ncbInstanceAdaptor<motion::ResourceManager>::GetNativeInstance(objthis);
    if(!manager) return TJS_E_INVALIDPARAM;
    manager->clearCache();
    return TJS_S_OK;
}

NCB_REGISTER_SUBCLASS(ResourceManager) {
    NCB_CONSTRUCTOR((iTJSDispatch2 *, tjs_int));
    NCB_METHOD(load);
    NCB_METHOD_RAW_CALLBACK(unload, ResourceManager_unload, 0);
    NCB_METHOD_RAW_CALLBACK(clearCache, ResourceManager_clearCache, 0);
    NCB_METHOD_RAW_CALLBACK(setEmotePSBDecryptSeed,
                            &ResourceManager::setEmotePSBDecryptSeed,
                            TJS_STATICMEMBER);
    NCB_METHOD_RAW_CALLBACK(setEmotePSBDecryptFunc,
                            &ResourceManager::setEmotePSBDecryptFunc,
                            TJS_STATICMEMBER);
}

class Motion {
public:
    static tjs_error getPlayFlagForce(tTJSVariant *r, tjs_int, tTJSVariant **,
                                      iTJSDispatch2 *) {
        if(r) *r = tTJSVariant(static_cast<tjs_int>(1));
        return TJS_S_OK;
    }

    static tjs_error getShapeTypePoint(tTJSVariant *r, tjs_int, tTJSVariant **,
                                       iTJSDispatch2 *) {
        if(r) *r = tTJSVariant(static_cast<tjs_int>(0));
        return TJS_S_OK;
    }

    static tjs_error getShapeTypeCircle(tTJSVariant *r, tjs_int, tTJSVariant **,
                                        iTJSDispatch2 *) {
        if(r) *r = tTJSVariant(static_cast<tjs_int>(1));
        return TJS_S_OK;
    }

    static tjs_error getShapeTypeRect(tTJSVariant *r, tjs_int, tTJSVariant **,
                                      iTJSDispatch2 *) {
        if(r) *r = tTJSVariant(static_cast<tjs_int>(2));
        return TJS_S_OK;
    }

    static tjs_error getShapeTypeQuad(tTJSVariant *r, tjs_int, tTJSVariant **,
                                      iTJSDispatch2 *) {
        if(r) *r = tTJSVariant(static_cast<tjs_int>(3));
        return TJS_S_OK;
    }

    static tjs_error setEnableD3D(tTJSVariant *, tjs_int count, tTJSVariant **p,
                                  iTJSDispatch2 *) {
        if(count == 1 && (*p)->Type() == tvtInteger) {
            _enableD3D = static_cast<bool>(**p);
            return TJS_S_OK;
        }
        return TJS_E_INVALIDPARAM;
    }

    static tjs_error getEnableD3D(tTJSVariant *r, tjs_int, tTJSVariant **,
                                  iTJSDispatch2 *) {
        iTJSDispatch2 *obj = TJSCreateDictionaryObject();
        if (obj) {
            *r = tTJSVariant(obj);
            obj->Release();
        } else {
            *r = tTJSVariant();
        }
        return TJS_S_OK;
    }

private:
    inline static bool _enableD3D;
};

class D3DAdaptorStub {
public:
    D3DAdaptorStub() = default;
    virtual ~D3DAdaptorStub() = default;
};

static tjs_error D3DAdaptorStub_Factory(
        D3DAdaptorStub **result, tjs_int /*numparams*/, tTJSVariant ** /*param*/,
        iTJSDispatch2 *objthis) {
    *result = new D3DAdaptorStub();
    if (objthis) {
        tTJSVariant m(TJS_W("missing"));
        objthis->ClassInstanceInfo(TJS_CII_SET_MISSING, 0, &m);
    }
    return TJS_S_OK;
}

NCB_REGISTER_SUBCLASS_DELAY(D3DAdaptorStub) {
    RawCallback(D3DAdaptorStub_Factory);
    NCB_METHOD_RAW_CALLBACK(missing, D3DAdaptorMissingHandler, 0);
    NCB_METHOD_RAW_CALLBACK(finalize, D3DAdaptorStub_noop, 0);
}

NCB_REGISTER_CLASS(Motion) {
    NCB_PROPERTY_RAW_CALLBACK(enableD3D, Motion::getEnableD3D,
                              Motion::setEnableD3D, TJS_STATICMEMBER);
    NCB_PROPERTY_RAW_CALLBACK_RO(PlayFlagForce, Motion::getPlayFlagForce, TJS_STATICMEMBER);
    NCB_PROPERTY_RAW_CALLBACK_RO(ShapeTypePoint, Motion::getShapeTypePoint, TJS_STATICMEMBER);
    NCB_PROPERTY_RAW_CALLBACK_RO(ShapeTypeCircle, Motion::getShapeTypeCircle, TJS_STATICMEMBER);
    NCB_PROPERTY_RAW_CALLBACK_RO(ShapeTypeRect, Motion::getShapeTypeRect, TJS_STATICMEMBER);
    NCB_PROPERTY_RAW_CALLBACK_RO(ShapeTypeQuad, Motion::getShapeTypeQuad, TJS_STATICMEMBER);
    NCB_SUBCLASS(ResourceManager, ResourceManager);
    NCB_SUBCLASS(Player, Player);
    NCB_SUBCLASS(EmotePlayer, EmotePlayer);
    NCB_SUBCLASS(SeparateLayerAdaptor, SeparateLayerAdaptor);
    NCB_SUBCLASS(D3DAdaptor, D3DAdaptorStub);
}

struct LayerCaptureCanvasStub {};

static tjs_error Layer_captureCanvas(tTJSVariant *r, tjs_int count, tTJSVariant **p, iTJSDispatch2 *objthis) {
    if(r) r->Clear();
    return TJS_S_OK;
}

NCB_ATTACH_CLASS_WITH_HOOK(LayerCaptureCanvasStub, Layer) {
    NCB_METHOD_RAW_CALLBACK(captureCanvas, Layer_captureCanvas, 0);
}

static void PostUnregistCallback() {}

NCB_POST_UNREGIST_CALLBACK(PostUnregistCallback);
