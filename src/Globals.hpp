#pragma once

#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/config/shared/animation/AnimationTree.hpp>
#include <hyprland/src/config/legacy/ConfigManager.hpp>
#include <hyprland/src/config/ConfigManager.hpp>
#include <hyprland/src/managers/input/InputManager.hpp>
#include <hyprland/src/layout/LayoutManager.hpp>
#include <hyprland/src/event/EventBus.hpp>
#include <hyprland/src/helpers/time/Time.hpp>
#include <hyprland/src/managers/animation/AnimationManager.hpp>
#include <hyprland/src/config/ConfigValue.hpp>
#include <hyprutils/signal/Signal.hpp>
#include <functional>
#include <tuple>
#include <type_traits>

using CFramebuffer = Render::GL::CGLFramebuffer;
using CTexture = Render::ITexture;
using IFramebuffer = Render::IFramebuffer;
using Render::eRenderPassMode;
using Render::SRenderModifData;
inline constexpr Render::eRenderPassMode RENDER_PASS_ALL = Render::RENDER_PASS_ALL;
inline constexpr Render::eRenderPassMode RENDER_PASS_MAIN = Render::RENDER_PASS_MAIN;
using Render::GL::g_pHyprOpenGL;

using SCallbackInfo = Event::SCallbackInfo;

template <typename T>
using HyprSignalRefArg = std::conditional_t<std::is_trivially_copyable_v<T>, T, const T&>;

template <typename EventType, typename Signal>
CHyprSignalListener listenCancellable(Signal& signal, std::function<void(const EventType&, SCallbackInfo&)> handler) {
    struct Hack : Hyprutils::Signal::CSignalBase {
        using CSignalBase::registerListenerInternal;
    };

    return reinterpret_cast<Hack&>(signal).registerListenerInternal([handler](void* args) {
        using Tuple = std::tuple<HyprSignalRefArg<EventType>, HyprSignalRefArg<Event::SCallbackInfo&>>;
        auto* tup   = static_cast<Tuple*>(args);
        handler(std::get<0>(*tup), std::get<1>(*tup));
    });
}

namespace HyprspaceCompat {
    inline void handleWorkspaceRules(const std::string& key, const std::string& value) {
        auto& configManager = ::Config::mgr();
        if (!configManager || configManager->type() != ::Config::CONFIG_LEGACY)
            return;

        auto* legacyConfigManager = dynamic_cast<::Config::Legacy::CConfigManager*>(configManager.get());
        if (legacyConfigManager)
            legacyConfigManager->handleWorkspaceRules(key, value);
    }
}

inline HANDLE pHandle = NULL;

typedef void (*tRenderWindow)(void*, PHLWINDOW, PHLMONITOR, const Time::steady_tp&, bool, eRenderPassMode, bool, bool);
extern void* pRenderWindow;
typedef void (*tRenderLayer)(void*, PHLLS, PHLMONITOR, const Time::steady_tp&, bool, bool);
extern void* pRenderLayer;
typedef void (*tRenderBackground)(void*, PHLMONITOR);
extern void* pRenderBackground;
extern bool g_renderHooksReady;
extern bool g_configReloading;
namespace Config {
    extern CHyprColor panelBaseColor;
    extern CHyprColor panelBorderColor;
    extern CHyprColor workspaceActiveBackground;
    extern CHyprColor workspaceInactiveBackground;
    extern CHyprColor workspaceActiveBorder;
    extern CHyprColor workspaceInactiveBorder;

    extern int panelHeight;
    extern int panelBorderWidth;
    extern int workspaceMargin;
    extern int reservedArea;
    extern int workspaceBorderSize;
    extern bool adaptiveHeight; // TODO: implement
    extern bool centerAligned;
    extern bool onBottom; // TODO: implement
    extern bool hideBackgroundLayers;
    extern bool hideTopLayers;
    extern bool hideOverlayLayers;
    extern bool drawActiveWorkspace;
    extern bool hideRealLayers;
    extern bool affectStrut;

    extern bool overrideGaps;
    extern int gapsIn;
    extern int gapsOut;

    extern bool autoDrag;
    extern bool autoScroll;
    extern bool exitOnClick;
    extern bool switchOnDrop;
    extern bool exitOnSwitch;
    extern bool showNewWorkspace;
    extern bool showEmptyWorkspace;
    extern bool showSpecialWorkspace;

    extern bool disableGestures;
    extern bool reverseSwipe;

    extern bool disableBlur;
    extern float overrideAnimSpeed;
    extern float dragAlpha;
}

extern int numWorkspaces;
