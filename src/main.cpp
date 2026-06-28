#include <hyprland/src/plugins/PluginSystem.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/devices/IKeyboard.hpp>
#include <hyprland/src/debug/log/Logger.hpp>
#include <hyprland/src/event/EventBus.hpp>
#include <hyprland/src/managers/SeatManager.hpp>
#include <hyprland/src/helpers/time/Time.hpp>
#include <hyprland/src/layout/LayoutManager.hpp>
#include <any>
#include <optional>
#include "Overview.hpp"
#include "Globals.hpp"

void* pRenderWindow;
void* pRenderLayer;
void* pRenderBackground;
bool  g_renderHooksReady = false;
bool  g_configReloading  = false;

std::vector<std::shared_ptr<CHyprspaceWidget>> g_overviewWidgets;


CHyprColor Config::panelBaseColor = CHyprColor(0, 0, 0, 0);
CHyprColor Config::panelBorderColor = CHyprColor(0, 0, 0, 0);
CHyprColor Config::workspaceActiveBackground = CHyprColor(0.05, 0.08, 0.11, 0.38);
CHyprColor Config::workspaceInactiveBackground = CHyprColor(0.05, 0.08, 0.11, 0.28);
CHyprColor Config::workspaceActiveBorder = CHyprColor(1, 1, 1, 0.3);
CHyprColor Config::workspaceInactiveBorder = CHyprColor(0.72, 0.75, 0.79, 0.22);

int Config::panelHeight = 250;
int Config::panelBorderWidth = 2;
int Config::workspaceMargin = 12;
int Config::reservedArea = 0;
int Config::workspaceBorderSize = 2;
bool Config::adaptiveHeight = false; // TODO: implement
bool Config::centerAligned = true;
bool Config::onBottom = false; // TODO: implement
bool Config::hideBackgroundLayers = false;
bool Config::hideTopLayers = false;
bool Config::hideOverlayLayers = false;
bool Config::drawActiveWorkspace = true;
bool Config::hideRealLayers = true;
bool Config::affectStrut = true;

bool Config::overrideGaps = false;
int Config::gapsIn = 20;
int Config::gapsOut = 60;

bool Config::autoDrag = true;
bool Config::autoScroll = true;
bool Config::exitOnClick = true;
bool Config::switchOnDrop = false;
bool Config::exitOnSwitch = false;
bool Config::showNewWorkspace = true;
bool Config::showEmptyWorkspace = true;
bool Config::showSpecialWorkspace = false;

bool Config::disableGestures = false;
bool Config::reverseSwipe = false;

bool Config::disableBlur = false;

float Config::overrideAnimSpeed = 0;

float Config::dragAlpha = 0.2;

int numWorkspaces = -1; //hyprsplit/split-monitor-workspaces support

namespace {
    template <typename T>
    std::optional<T> getPluginConfigValue(const char* key) {
        const auto cfg = HyprlandAPI::getConfigValue(pHandle, key);
        if (!cfg) {
            Log::logger->log(Log::WARN, std::string{"Hyprspace: missing config value "} + key);
            return std::nullopt;
        }

        try {
            return std::any_cast<T>(cfg->getValue());
        } catch (const std::bad_any_cast&) {
            Log::logger->log(Log::ERR, std::string{"Hyprspace: invalid config type for "} + key);
            return std::nullopt;
        }
    }

    template <typename T>
    void loadPluginConfig(const char* key, T& target) {
        if (const auto value = getPluginConfigValue<T>(key); value.has_value())
            target = *value;
    }

    void loadPluginColor(const char* key, CHyprColor& target) {
        if (const auto value = getPluginConfigValue<Hyprlang::INT>(key); value.has_value())
            target = CHyprColor(*value);
    }
}

CHyprSignalListener g_pRenderHook;
CHyprSignalListener g_pConfigReloadHook;
CHyprSignalListener g_pOpenLayerHook;
CHyprSignalListener g_pCloseLayerHook;
CHyprSignalListener g_pMouseButtonHook;
CHyprSignalListener g_pMouseAxisHook;
CHyprSignalListener g_pTouchDownHook;
CHyprSignalListener g_pTouchMoveHook;
CHyprSignalListener g_pTouchUpHook;
CHyprSignalListener g_pSwipeBeginHook;
CHyprSignalListener g_pSwipeUpdateHook;
CHyprSignalListener g_pSwipeEndHook;
CHyprSignalListener g_pKeyPressHook;
CHyprSignalListener g_pSwitchWorkspaceHook;
CHyprSignalListener g_pAddMonitorHook;
CHyprSignalListener g_pStartHook;

APICALL EXPORT std::string PLUGIN_API_VERSION() {
    return HYPRLAND_API_VERSION;
}

std::shared_ptr<CHyprspaceWidget> getWidgetForMonitor(PHLMONITORREF pMonitor) {
    for (auto& widget : g_overviewWidgets) {
        if (!widget) continue;
        if (!widget->getOwner()) continue;
        if (widget->getOwner() == pMonitor) {
            return widget;
        }
    }
    return nullptr;
}

// used to enforce the layout
void refreshWidgets() {
    for (auto& widget : g_overviewWidgets) {
        if (widget != nullptr)
            if (widget->isActive())
                widget->show();
    }
}

bool g_layoutNeedsRefresh = true;

// for restroing dragged window's alpha value
float g_oAlpha = -1;

void onRender(eRenderStage renderStage) {
    if (!g_pHyprOpenGL || !g_pHyprRenderer)
        return;

    const auto currentMonitor = g_pHyprRenderer->m_renderData.pMonitor;
    if ((renderStage == eRenderStage::RENDER_PRE_WINDOWS || renderStage == eRenderStage::RENDER_POST_WINDOWS || renderStage == eRenderStage::RENDER_LAST_MOMENT) &&
        !currentMonitor) {
        g_oAlpha = -1;
        return;
    }

    // refresh layout after scheduled recalculation on monitors were carried out in renderMonitor
    if (renderStage == eRenderStage::RENDER_PRE) {
        if (g_layoutNeedsRefresh) {
            refreshWidgets();
            g_layoutNeedsRefresh = false;
        }
    }
    else if (renderStage == eRenderStage::RENDER_PRE_WINDOWS) {


        const auto widget = getWidgetForMonitor(currentMonitor);
        if (widget != nullptr)
            if (widget->getOwner()) {
                //widget->draw();
                PHLWINDOW curWindow;
                if (const auto dragTarget = g_layoutManager->dragController()->target())
                    curWindow = dragTarget->window();
                if (curWindow) {
                    if (widget->isActive()) {
                        g_oAlpha = curWindow->alphaGoal(Desktop::View::WINDOW_ALPHA_ACTIVE);
                        curWindow->alpha(Desktop::View::WINDOW_ALPHA_ACTIVE)->setValueAndWarp(0); // HACK: hide dragged window for the actual pass
                    }
                }
                else g_oAlpha = -1;
            }
            else g_oAlpha = -1;
        else g_oAlpha = -1;

    }
    else if (renderStage == eRenderStage::RENDER_POST_WINDOWS) {

        const auto widget = getWidgetForMonitor(currentMonitor);

        if (widget != nullptr)
            if (widget->getOwner()) {
                if (widget->isActive() && currentMonitor->m_activeWorkspace) {
                    SP<IFramebuffer> sourceFramebuffer = g_pHyprRenderer->m_renderData.currentFB;
                    if ((!sourceFramebuffer || !sourceFramebuffer->isAllocated()) && g_pHyprRenderer->m_renderData.mainFB &&
                        g_pHyprRenderer->m_renderData.mainFB->isAllocated())
                        sourceFramebuffer = g_pHyprRenderer->m_renderData.mainFB;

                    if (sourceFramebuffer && sourceFramebuffer->isAllocated())
                        widget->captureOverviewMonitorSnapshot(sourceFramebuffer, currentMonitor->activeWorkspaceID());
                }

                widget->draw();
                if (g_oAlpha != -1) {
                    PHLWINDOW curWindow;
                    if (const auto dragTarget = g_layoutManager->dragController()->target())
                        curWindow = dragTarget->window();
                    if (curWindow && pRenderWindow) {
                        curWindow->alpha(Desktop::View::WINDOW_ALPHA_ACTIVE)->setValueAndWarp(Config::dragAlpha);
                        curWindow->m_ruleApplicator->noBlur().unset(Desktop::Types::PRIORITY_SET_PROP);
                        const auto time = Time::steadyNow();
                        (*(tRenderWindow)pRenderWindow)(g_pHyprRenderer.get(), curWindow, widget->getOwner(), time, true, RENDER_PASS_MAIN, false, false);
                        curWindow->m_ruleApplicator->noBlur().unset(Desktop::Types::PRIORITY_SET_PROP);
                        curWindow->alpha(Desktop::View::WINDOW_ALPHA_ACTIVE)->setValueAndWarp(g_oAlpha);
                    }
                }
                g_oAlpha = -1;
            }

    }
    else if (renderStage == eRenderStage::RENDER_LAST_MOMENT) {
        const auto widget = getWidgetForMonitor(currentMonitor);

        if (!widget || !widget->getOwner() || widget->isActive() || widget->curYOffset->isBeingAnimated())
            return;

        SP<IFramebuffer> sourceFramebuffer = g_pHyprRenderer->m_renderData.currentFB;
        if ((!sourceFramebuffer || !sourceFramebuffer->isAllocated()) && g_pHyprRenderer->m_renderData.mainFB && g_pHyprRenderer->m_renderData.mainFB->isAllocated())
            sourceFramebuffer = g_pHyprRenderer->m_renderData.mainFB;

        if (!sourceFramebuffer || !sourceFramebuffer->isAllocated() || !currentMonitor->m_activeWorkspace)
            return;

        widget->captureOverviewMonitorSnapshot(sourceFramebuffer, currentMonitor->activeWorkspaceID());
    }
}

// event hook, currently this is only here to re-hide top layer panels on workspace change
void onWorkspaceChange(const PHLWORKSPACE& pWorkspace) {

    if (!pWorkspace) return;

    auto widget = getWidgetForMonitor(g_pCompositor->getMonitorFromID(pWorkspace->m_monitor->m_id));
    if (widget != nullptr)
        if (widget->isActive())
            widget->show();
}

// event hook for click and drag interaction
void onMouseButton(IPointer::SButtonEvent e, Event::SCallbackInfo& info) {

    if (e.button != BTN_LEFT) return;

    const auto pressed = e.state == WL_POINTER_BUTTON_STATE_PRESSED;
    const auto pMonitor = g_pCompositor->getMonitorFromCursor();
    if (pMonitor) {
        const auto widget = getWidgetForMonitor(pMonitor);
        if (widget) {
            if (widget->isActive()) {
                info.cancelled = !widget->buttonEvent(pressed, g_pInputManager->getMouseCoordsInternal());
            }
        }
    }

}

// event hook for scrolling through panel and workspaces
void onMouseAxis(IPointer::SAxisEvent e, Event::SCallbackInfo& info) {

    const auto pMonitor = g_pCompositor->getMonitorFromCursor();
    if (pMonitor) {
        const auto widget = getWidgetForMonitor(pMonitor);
        if (widget) {
            if (widget->isActive()) {
                info.cancelled = !widget->axisEvent(e.delta, e.axis, g_pInputManager->getMouseCoordsInternal());
            }
        }
    }

}

// event hook for swipe
void onSwipeBegin(IPointer::SSwipeBeginEvent e, Event::SCallbackInfo& info) {

    if (Config::disableGestures) return;

    const auto widget = getWidgetForMonitor(g_pCompositor->getMonitorFromCursor());
    if (widget != nullptr)
        widget->beginSwipe(e);

    // end other widget swipe
    for (auto& w : g_overviewWidgets) {
        if (w != widget && w->isSwiping()) {
            IPointer::SSwipeEndEvent dummy;
            dummy.cancelled = true;
            w->endSwipe(dummy);
        }
    }
}

// event hook for update swipe, most of the swiping mechanics are here
void onSwipeUpdate(IPointer::SSwipeUpdateEvent e, Event::SCallbackInfo& info) {

    if (Config::disableGestures) return;

    const auto widget = getWidgetForMonitor(g_pCompositor->getMonitorFromCursor());
    if (widget != nullptr)
        info.cancelled = !widget->updateSwipe(e);
}

// event hook for end swipe
void onSwipeEnd(IPointer::SSwipeEndEvent e, Event::SCallbackInfo& info) {

    if (Config::disableGestures) return;

    const auto widget = getWidgetForMonitor(g_pCompositor->getMonitorFromCursor());
    if (widget != nullptr)
        widget->endSwipe(e);
}

// Close overview with configurable key
void onKeyPress(IKeyboard::SKeyEvent e, Event::SCallbackInfo& info) {
    if (e.state != WL_KEYBOARD_KEY_STATE_PRESSED)
        return;

    const auto k = g_pSeatManager->m_keyboard.lock();
    if (!k || !k->m_xkbSymState)
        return;

    const auto exitKeyValue = HyprlandAPI::getConfigValue(pHandle, "plugin:overview:exitKey");
    if (!exitKeyValue)
        return;

    const auto keycode = e.keycode + 8; // Because to xkbcommon it's +8 from libinput
    const xkb_keysym_t keysym = xkb_state_key_get_one_sym(k->m_xkbSymState, keycode);

    // Get configured exit key (default to Escape if not configured)
    const auto cfgExitKey = std::any_cast<Hyprlang::STRING>(exitKeyValue->getValue());
    if (!cfgExitKey || cfgExitKey[0] == '\0')
        return;

    const xkb_keysym_t cfgExitKeysym = xkb_keysym_from_name(cfgExitKey, XKB_KEYSYM_CASE_INSENSITIVE);
    if (cfgExitKeysym == XKB_KEY_NoSymbol)
        return;

    if (keysym == cfgExitKeysym) {
        // close all panels
        bool overviewActive = false;
        for (auto& widget : g_overviewWidgets) {
            if (widget != nullptr && widget->isActive()) {
                widget->hide();
                overviewActive = true;
            }
        }
        // Only cancel event if overview was active and closed
        if (overviewActive)
            info.cancelled = true;
    }
}

PHLMONITOR g_pTouchedMonitor;

void onTouchDown(ITouch::SDownEvent e, Event::SCallbackInfo& info) {
    auto targetMonitor = g_pCompositor->getMonitorFromName(!e.device->m_boundOutput.empty() ? e.device->m_boundOutput : "");
    targetMonitor = targetMonitor ? targetMonitor : g_pCompositor->getMonitorFromCursor();

    const auto widget = getWidgetForMonitor(targetMonitor);
    if (widget != nullptr && targetMonitor != nullptr) {
        if (widget->isActive()) {
            Vector2D pos = targetMonitor->m_position + e.pos * targetMonitor->m_size;
            info.cancelled = !widget->buttonEvent(true, pos);
            if (info.cancelled) {
                g_pTouchedMonitor = targetMonitor;
                g_pCompositor->warpCursorTo(pos);
                g_pInputManager->refocus();
            }
        }
    }
}

void onTouchMove(ITouch::SMotionEvent e, Event::SCallbackInfo& info) {
    if (g_pTouchedMonitor == nullptr) return;

    g_pCompositor->warpCursorTo(g_pTouchedMonitor->m_position + g_pTouchedMonitor->m_size * e.pos);
    g_pInputManager->simulateMouseMovement();
}

void onTouchUp(ITouch::SUpEvent e, Event::SCallbackInfo& info) {
    const auto widget = getWidgetForMonitor(g_pTouchedMonitor);
    if (widget != nullptr && g_pTouchedMonitor != nullptr)
        if (widget->isActive())
            info.cancelled = !widget->buttonEvent(false, g_pInputManager->getMouseCoordsInternal());

    g_pTouchedMonitor = nullptr;
}

static SDispatchResult dispatchToggleOverview(std::string arg) {
    auto currentMonitor = g_pCompositor->getMonitorFromCursor();
    auto widget = getWidgetForMonitor(currentMonitor);
    if (widget) {
        if (arg.contains("all")) {
            if (widget->isActive()) {
                for (auto& widget : g_overviewWidgets) {
                    if (widget != nullptr)
                        if (widget->isActive())
                            widget->hide();
                }
            }
            else {
                for (auto& widget : g_overviewWidgets) {
                    if (widget != nullptr)
                        if (!widget->isActive())
                            widget->show();
                }
            }
        }
        else
            widget->isActive() ? widget->hide() : widget->show();
    }
    return SDispatchResult{};
}

static SDispatchResult dispatchOpenOverview(std::string arg) {
    if (arg.contains("all")) {
        for (auto& widget : g_overviewWidgets) {
            if (!widget->isActive()) widget->show();
        }
    }
    else {
        auto currentMonitor = g_pCompositor->getMonitorFromCursor();
        auto widget = getWidgetForMonitor(currentMonitor);
        if (widget)
            if (!widget->isActive()) widget->show();
    }
    return SDispatchResult{};
}

static SDispatchResult dispatchCloseOverview(std::string arg) {
    if (arg.contains("all")) {
        for (auto& widget : g_overviewWidgets) {
            if (widget->isActive()) widget->hide();
        }
    }
    else {
        auto currentMonitor = g_pCompositor->getMonitorFromCursor();
        auto widget = getWidgetForMonitor(currentMonitor);
        if (widget)
            if (widget->isActive()) widget->hide();
    }
    return SDispatchResult{};
}

void* findFunctionBySymbol(HANDLE inHandle, const std::string func, const std::string sym) {
    // should return all functions
    auto funcSearch = HyprlandAPI::findFunctionsByName(inHandle, func);
    for (auto f : funcSearch) {
        if (f.demangled.contains(sym))
            return f.address;
    }
    return nullptr;
}

void reloadConfig() {
    g_configReloading = true;

    loadPluginColor("plugin:overview:panelColor", Config::panelBaseColor);
    loadPluginColor("plugin:overview:panelBorderColor", Config::panelBorderColor);
    loadPluginColor("plugin:overview:workspaceActiveBackground", Config::workspaceActiveBackground);
    loadPluginColor("plugin:overview:workspaceInactiveBackground", Config::workspaceInactiveBackground);
    loadPluginColor("plugin:overview:workspaceActiveBorder", Config::workspaceActiveBorder);
    loadPluginColor("plugin:overview:workspaceInactiveBorder", Config::workspaceInactiveBorder);

    loadPluginConfig("plugin:overview:panelHeight", Config::panelHeight);
    loadPluginConfig("plugin:overview:panelBorderWidth", Config::panelBorderWidth);
    loadPluginConfig("plugin:overview:workspaceMargin", Config::workspaceMargin);
    loadPluginConfig("plugin:overview:reservedArea", Config::reservedArea);
    loadPluginConfig("plugin:overview:workspaceBorderSize", Config::workspaceBorderSize);
    loadPluginConfig("plugin:overview:adaptiveHeight", Config::adaptiveHeight);
    loadPluginConfig("plugin:overview:centerAligned", Config::centerAligned);
    loadPluginConfig("plugin:overview:onBottom", Config::onBottom);
    loadPluginConfig("plugin:overview:hideBackgroundLayers", Config::hideBackgroundLayers);
    loadPluginConfig("plugin:overview:hideTopLayers", Config::hideTopLayers);
    loadPluginConfig("plugin:overview:hideOverlayLayers", Config::hideOverlayLayers);
    loadPluginConfig("plugin:overview:drawActiveWorkspace", Config::drawActiveWorkspace);
    loadPluginConfig("plugin:overview:hideRealLayers", Config::hideRealLayers);
    loadPluginConfig("plugin:overview:affectStrut", Config::affectStrut);

    loadPluginConfig("plugin:overview:overrideGaps", Config::overrideGaps);
    loadPluginConfig("plugin:overview:gapsIn", Config::gapsIn);
    loadPluginConfig("plugin:overview:gapsOut", Config::gapsOut);

    loadPluginConfig("plugin:overview:autoDrag", Config::autoDrag);
    loadPluginConfig("plugin:overview:autoScroll", Config::autoScroll);
    loadPluginConfig("plugin:overview:exitOnClick", Config::exitOnClick);
    loadPluginConfig("plugin:overview:switchOnDrop", Config::switchOnDrop);
    loadPluginConfig("plugin:overview:exitOnSwitch", Config::exitOnSwitch);
    loadPluginConfig("plugin:overview:showNewWorkspace", Config::showNewWorkspace);
    loadPluginConfig("plugin:overview:showEmptyWorkspace", Config::showEmptyWorkspace);
    loadPluginConfig("plugin:overview:showSpecialWorkspace", Config::showSpecialWorkspace);

    loadPluginConfig("plugin:overview:disableGestures", Config::disableGestures);
    loadPluginConfig("plugin:overview:reverseSwipe", Config::reverseSwipe);

    loadPluginConfig("plugin:overview:disableBlur", Config::disableBlur);
    loadPluginConfig("plugin:overview:overrideAnimSpeed", Config::overrideAnimSpeed);
    
    // We don't need to store exitKey in Config namespace as it's only used in onKeyPress

    for (auto& widget : g_overviewWidgets) {
        if (!widget)
            continue;

        widget->updateConfig();
        if (widget->isActive() || widget->isSwiping()) {
            widget->hide();
            IPointer::SSwipeEndEvent dummy;
            dummy.cancelled = true;
            widget->endSwipe(dummy);
        }
    }

    loadPluginConfig("plugin:overview:dragAlpha", Config::dragAlpha);

    // get number of workspaces from hyprsplit or split-monitor-workspaces plugin config
    Hyprlang::CConfigValue* numWorkspacesConfig = HyprlandAPI::getConfigValue(pHandle, "plugin:hyprsplit:num_workspaces");
    if (!numWorkspacesConfig)
        numWorkspacesConfig = HyprlandAPI::getConfigValue(pHandle, "plugin:split-monitor-workspaces:count");
    if (numWorkspacesConfig)
        numWorkspaces = std::any_cast<Hyprlang::INT>(numWorkspacesConfig->getValue());

    // TODO: schedule frame for monitor?

    g_configReloading = false;
}

void registerMonitors() {
    // create a widget for each monitor
    for (auto& m : g_pCompositor->m_monitors) {
        if (getWidgetForMonitor(m) != nullptr) continue;
        CHyprspaceWidget* widget = new CHyprspaceWidget(m->m_id);
        g_overviewWidgets.emplace_back(widget);
    }
}

APICALL EXPORT PLUGIN_DESCRIPTION_INFO PLUGIN_INIT(HANDLE inHandle) {
    pHandle = inHandle;

    Log::logger->log(Log::DEBUG, "Loading overview plugin");

    HyprlandAPI::addConfigValue(pHandle, "plugin:overview:panelColor", Hyprlang::INT{CHyprColor(0, 0, 0, 0).getAsHex()});
    HyprlandAPI::addConfigValue(pHandle, "plugin:overview:panelBorderColor", Hyprlang::INT{CHyprColor(0, 0, 0, 0).getAsHex()});
    HyprlandAPI::addConfigValue(pHandle, "plugin:overview:workspaceActiveBackground", Hyprlang::INT{CHyprColor(0.05, 0.08, 0.11, 0.38).getAsHex()});
    HyprlandAPI::addConfigValue(pHandle, "plugin:overview:workspaceInactiveBackground", Hyprlang::INT{CHyprColor(0.05, 0.08, 0.11, 0.28).getAsHex()});
    HyprlandAPI::addConfigValue(pHandle, "plugin:overview:workspaceActiveBorder", Hyprlang::INT{CHyprColor(1, 1, 1, 0.25).getAsHex()});
    HyprlandAPI::addConfigValue(pHandle, "plugin:overview:workspaceInactiveBorder", Hyprlang::INT{CHyprColor(0.72, 0.75, 0.79, 0.22).getAsHex()});

    HyprlandAPI::addConfigValue(pHandle, "plugin:overview:panelHeight", Hyprlang::INT{250});
    HyprlandAPI::addConfigValue(pHandle, "plugin:overview:panelBorderWidth", Hyprlang::INT{2});
    HyprlandAPI::addConfigValue(pHandle, "plugin:overview:workspaceMargin", Hyprlang::INT{12});
    HyprlandAPI::addConfigValue(pHandle, "plugin:overview:workspaceBorderSize", Hyprlang::INT{2});
    HyprlandAPI::addConfigValue(pHandle, "plugin:overview:reservedArea", Hyprlang::INT{0});
    HyprlandAPI::addConfigValue(pHandle, "plugin:overview:adaptiveHeight", Hyprlang::INT{0});
    HyprlandAPI::addConfigValue(pHandle, "plugin:overview:centerAligned", Hyprlang::INT{1});
    HyprlandAPI::addConfigValue(pHandle, "plugin:overview:onBottom", Hyprlang::INT{0});
    HyprlandAPI::addConfigValue(pHandle, "plugin:overview:hideBackgroundLayers", Hyprlang::INT{0});
    HyprlandAPI::addConfigValue(pHandle, "plugin:overview:hideTopLayers", Hyprlang::INT{0});
    HyprlandAPI::addConfigValue(pHandle, "plugin:overview:hideOverlayLayers", Hyprlang::INT{0});
    HyprlandAPI::addConfigValue(pHandle, "plugin:overview:drawActiveWorkspace", Hyprlang::INT{1});
    HyprlandAPI::addConfigValue(pHandle, "plugin:overview:hideRealLayers", Hyprlang::INT{1});
    HyprlandAPI::addConfigValue(pHandle, "plugin:overview:affectStrut", Hyprlang::INT{1});

    HyprlandAPI::addConfigValue(pHandle, "plugin:overview:overrideGaps", Hyprlang::INT{0});
    HyprlandAPI::addConfigValue(pHandle, "plugin:overview:gapsIn", Hyprlang::INT{20});
    HyprlandAPI::addConfigValue(pHandle, "plugin:overview:gapsOut", Hyprlang::INT{60});

    HyprlandAPI::addConfigValue(pHandle, "plugin:overview:autoDrag", Hyprlang::INT{1});
    HyprlandAPI::addConfigValue(pHandle, "plugin:overview:autoScroll", Hyprlang::INT{1});
    HyprlandAPI::addConfigValue(pHandle, "plugin:overview:exitOnClick", Hyprlang::INT{1});
    HyprlandAPI::addConfigValue(pHandle, "plugin:overview:switchOnDrop", Hyprlang::INT{0});
    HyprlandAPI::addConfigValue(pHandle, "plugin:overview:exitOnSwitch", Hyprlang::INT{0});
    HyprlandAPI::addConfigValue(pHandle, "plugin:overview:showNewWorkspace", Hyprlang::INT{1});
    HyprlandAPI::addConfigValue(pHandle, "plugin:overview:showEmptyWorkspace", Hyprlang::INT{1});
    HyprlandAPI::addConfigValue(pHandle, "plugin:overview:showSpecialWorkspace", Hyprlang::INT{0});

    HyprlandAPI::addConfigValue(pHandle, "plugin:overview:disableGestures", Hyprlang::INT{1});
    HyprlandAPI::addConfigValue(pHandle, "plugin:overview:reverseSwipe", Hyprlang::INT{0});

    HyprlandAPI::addConfigValue(pHandle, "plugin:overview:disableBlur", Hyprlang::INT{0});
    HyprlandAPI::addConfigValue(pHandle, "plugin:overview:overrideAnimSpeed", Hyprlang::FLOAT{0.0});
    HyprlandAPI::addConfigValue(pHandle, "plugin:overview:dragAlpha", Hyprlang::FLOAT{0.2});
    HyprlandAPI::addConfigValue(pHandle, "plugin:overview:exitKey", Hyprlang::STRING{"Escape"});

    g_pConfigReloadHook = Event::bus()->m_events.config.reloaded.listen([] { reloadConfig(); });
    g_pStartHook = Event::bus()->m_events.start.listen([] {
        reloadConfig();
        registerMonitors();
    });
    HyprlandAPI::reloadConfig();

    HyprlandAPI::addDispatcherV2(pHandle, "overview:toggle", ::dispatchToggleOverview);
    HyprlandAPI::addDispatcherV2(pHandle, "overview:open", ::dispatchOpenOverview);
    HyprlandAPI::addDispatcherV2(pHandle, "overview:close", ::dispatchCloseOverview);

    g_pRenderHook = Event::bus()->m_events.render.stage.listen(onRender);

    // refresh on layer change
    g_pOpenLayerHook = Event::bus()->m_events.layer.opened.listen([](const PHLLS&) { g_layoutNeedsRefresh = true; });
    g_pCloseLayerHook = Event::bus()->m_events.layer.closed.listen([](const PHLLS&) { g_layoutNeedsRefresh = true; });

    g_pMouseButtonHook = listenCancellable<IPointer::SButtonEvent>(Event::bus()->m_events.input.mouse.button, onMouseButton);
    g_pMouseAxisHook = listenCancellable<IPointer::SAxisEvent>(Event::bus()->m_events.input.mouse.axis, onMouseAxis);

    g_pTouchDownHook = listenCancellable<ITouch::SDownEvent>(Event::bus()->m_events.input.touch.down, onTouchDown);
    g_pTouchMoveHook = listenCancellable<ITouch::SMotionEvent>(Event::bus()->m_events.input.touch.motion, onTouchMove);
    g_pTouchUpHook = listenCancellable<ITouch::SUpEvent>(Event::bus()->m_events.input.touch.up, onTouchUp);

    g_pSwipeBeginHook = listenCancellable<IPointer::SSwipeBeginEvent>(Event::bus()->m_events.gesture.swipe.begin, onSwipeBegin);
    g_pSwipeUpdateHook = listenCancellable<IPointer::SSwipeUpdateEvent>(Event::bus()->m_events.gesture.swipe.update, onSwipeUpdate);
    g_pSwipeEndHook = listenCancellable<IPointer::SSwipeEndEvent>(Event::bus()->m_events.gesture.swipe.end, onSwipeEnd);

    g_pKeyPressHook = listenCancellable<IKeyboard::SKeyEvent>(Event::bus()->m_events.input.keyboard.key, onKeyPress);

    g_pSwitchWorkspaceHook = Event::bus()->m_events.workspace.active.listen(onWorkspaceChange);

    pRenderWindow = findFunctionBySymbol(pHandle, "renderWindow", "Render::IHyprRenderer::renderWindow");
    if (!pRenderWindow)
        pRenderWindow = findFunctionBySymbol(pHandle, "renderWindow", "CHyprRenderer::renderWindow");
    pRenderLayer = findFunctionBySymbol(pHandle, "renderLayer", "Render::IHyprRenderer::renderLayer");
    if (!pRenderLayer)
        pRenderLayer = findFunctionBySymbol(pHandle, "renderLayer", "CHyprRenderer::renderLayer");
    pRenderBackground = findFunctionBySymbol(pHandle, "renderBackground", "Render::IHyprRenderer::renderBackground");
    if (!pRenderBackground)
        pRenderBackground = findFunctionBySymbol(pHandle, "renderBackground", "CHyprRenderer::renderBackground");
    g_renderHooksReady = pRenderWindow && pRenderLayer;

    registerMonitors();
    g_pAddMonitorHook = Event::bus()->m_events.monitor.added.listen([](const PHLMONITOR&) { registerMonitors(); });

    return {"Hyprspace", "Native workspace overview for Hyprland 0.55", "KZdkm+tco", "0.55.4-native.6"};
}

APICALL EXPORT void PLUGIN_EXIT() {
    g_pRenderHook.reset();
    g_pConfigReloadHook.reset();
    g_pOpenLayerHook.reset();
    g_pCloseLayerHook.reset();
    g_pMouseButtonHook.reset();
    g_pMouseAxisHook.reset();
    g_pTouchDownHook.reset();
    g_pTouchMoveHook.reset();
    g_pTouchUpHook.reset();
    g_pSwipeBeginHook.reset();
    g_pSwipeUpdateHook.reset();
    g_pSwipeEndHook.reset();
    g_pKeyPressHook.reset();
    g_pSwitchWorkspaceHook.reset();
    g_pAddMonitorHook.reset();
    g_pStartHook.reset();

    g_overviewWidgets.clear();
}
