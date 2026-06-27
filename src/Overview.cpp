#include "Overview.hpp"
#include "Globals.hpp"

void CHyprspaceWidget::clearOverviewMonitorSnapshot() {
    overviewMonitorSnapshotValid = false;
    overviewMonitorSnapshotWorkspaceID = WORKSPACE_INVALID;

    if (overviewMonitorSnapshot.isAllocated())
        overviewMonitorSnapshot.release();
}

void CHyprspaceWidget::captureOverviewWindowSnapshots() {
    if (!overviewWindowSnapshots.empty())
        return;

    const auto owner = getOwner();
    if (!owner)
        return;

    overviewWindowSnapshots.clear();

    for (auto& w : g_pCompositor->m_windows) {
        if (!w || !w->m_isMapped || !w->m_workspace || !w->m_workspace->m_monitor)
            continue;

        if (w->m_workspace->m_monitor->m_id != ownerID)
            continue;

        overviewWindowSnapshots.push_back(SWindowGeometrySnapshot{
            .window = PHLWINDOWREF(w),
            .workspaceID = w->m_workspace->m_id,
            .box = CBox({w->m_realPosition->value(), w->m_realSize->value()}),
        });
    }
}

void CHyprspaceWidget::clearOverviewWindowSnapshots() {
    overviewWindowSnapshots.clear();
}

std::optional<CBox> CHyprspaceWidget::getOverviewWindowSnapshotBox(PHLWINDOW window, PHLWORKSPACE workspace) const {
    if (!window || !workspace)
        return std::nullopt;

    for (const auto& snapshot : overviewWindowSnapshots) {
        if (snapshot.workspaceID != workspace->m_id)
            continue;

        if (snapshot.window.lock() == window)
            return snapshot.box;
    }

    return std::nullopt;
}

void CHyprspaceWidget::captureOverviewMonitorSnapshot(SP<IFramebuffer> sourceFramebuffer, WORKSPACEID workspaceID) {
    if (!sourceFramebuffer || !sourceFramebuffer->isAllocated())
        return;

    const auto owner = getOwner();
    if (!owner)
        return;

    const auto sourceSize = sourceFramebuffer->m_size;
    const int  width      = std::max(0, static_cast<int>(std::round(sourceSize.x)));
    const int  height     = std::max(0, static_cast<int>(std::round(sourceSize.y)));

    if (!(width > 0) || !(height > 0))
        return;

    const bool needsRealloc = !overviewMonitorSnapshot.isAllocated() ||
        static_cast<int>(std::round(overviewMonitorSnapshot.m_size.x)) != width ||
        static_cast<int>(std::round(overviewMonitorSnapshot.m_size.y)) != height;

    if (needsRealloc) {
        clearOverviewMonitorSnapshot();

        if (!overviewMonitorSnapshot.alloc(width, height))
            return;
    }

    GLint prevReadFramebuffer = 0;
    GLint prevDrawFramebuffer = 0;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevReadFramebuffer);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevDrawFramebuffer);

    glBindFramebuffer(GL_READ_FRAMEBUFFER, dc<Render::GL::CGLFramebuffer*>(sourceFramebuffer.get())->getFBID());
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, overviewMonitorSnapshot.getFBID());
    glBlitFramebuffer(0, 0, width, height, 0, 0, width, height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, prevReadFramebuffer);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, prevDrawFramebuffer);

    overviewMonitorSnapshotWorkspaceID = workspaceID;
    overviewMonitorSnapshotValid       = true;
}

bool CHyprspaceWidget::hasOverviewMonitorSnapshot(PHLWORKSPACE workspace) {
    return workspace && overviewMonitorSnapshotValid && overviewMonitorSnapshotWorkspaceID == workspace->m_id && overviewMonitorSnapshot.isAllocated() &&
        overviewMonitorSnapshot.getTexture();
}

SP<CTexture> CHyprspaceWidget::getOverviewMonitorSnapshotTexture(PHLWORKSPACE workspace) {
    if (!hasOverviewMonitorSnapshot(workspace))
        return {};

    return overviewMonitorSnapshot.getTexture();
}

CHyprspaceWidget::CHyprspaceWidget(uint64_t inOwnerID) {
    ownerID = inOwnerID;

    curAnimationConfig = *::Config::animationTree()->getAnimationPropertyConfig("windows");

    // the fuck is pValues???
    curAnimation = *curAnimationConfig.pValues.lock();
    *curAnimationConfig.pValues.lock() = curAnimation;

    if (Config::overrideAnimSpeed > 0)
        curAnimation.internalSpeed = Config::overrideAnimSpeed;

    g_pAnimationManager->createAnimation(0.F, curYOffset, curAnimationConfig.pValues.lock(), AVARDAMAGE_ENTIRE);
    curYOffset->setCallbackOnEnd([this](auto) {
        if (!active) {
            auto owner = getOwner();
            if (owner) {
                g_pHyprRenderer->damageMonitor(owner);
                for (auto& ws : g_pCompositor->getWorkspaces()) {
                    if (!ws || ws->m_monitor->m_id != ownerID) continue;
                    for (auto& w : g_pCompositor->m_windows) {
                        if (!w || w->m_workspace != ws || !w->m_isMapped) continue;
                        g_pHyprRenderer->damageWindow(w);
                    }
                }
                g_pCompositor->scheduleFrameForMonitor(owner);
            }
        }
    }, false);
    g_pAnimationManager->createAnimation(0.F, workspaceScrollOffset, curAnimationConfig.pValues.lock(), AVARDAMAGE_ENTIRE);
    curYOffset->setValueAndWarp(Config::panelHeight);
    workspaceScrollOffset->setValueAndWarp(0);
}

// TODO: implement deconstructor and delete widget on monitor unplug
CHyprspaceWidget::~CHyprspaceWidget() {}

PHLMONITOR CHyprspaceWidget::getOwner() {
    return g_pCompositor->getMonitorFromID(ownerID);
}

void CHyprspaceWidget::hideRealLayersForOverview() {
    if (!Config::hideRealLayers)
        return;

    auto owner = getOwner();
    if (!owner)
        return;

    const auto hideLayer = [this](PHLLS layer) {
        if (!layer || layer->m_readyToDelete || !layer->m_mapped)
            return;

        const auto savedAlpha = std::find_if(oLayerAlpha.begin(), oLayerAlpha.end(), [&](const auto& tuple) { return std::get<0>(tuple) == layer; });
        if (savedAlpha == oLayerAlpha.end())
            oLayerAlpha.emplace_back(std::make_tuple(layer, layer->m_alpha->goal()));

        layer->m_alpha->setValueAndWarp(0.F);
        *layer->m_alpha = 0.F;
    };

    for (auto& ls : owner->m_layerSurfaceLayers[2])
        hideLayer(ls.lock());
    for (auto& ls : owner->m_layerSurfaceLayers[3])
        hideLayer(ls.lock());
}

void CHyprspaceWidget::restoreRealLayersFromOverview() {
    for (auto& [layer, alpha] : oLayerAlpha) {
        if (!layer || layer->m_readyToDelete)
            continue;

        *layer->m_alpha = alpha;
    }

    oLayerAlpha.clear();
}

void CHyprspaceWidget::show() {
    auto owner = getOwner();
    if (!owner) return;

    if (!active)
        captureOverviewWindowSnapshots();

    if (prevFullscreen.empty()) {
        // unfullscreen all windows
        for (auto& ws : g_pCompositor->getWorkspaces()) {
            if (ws->m_monitor->m_id == ownerID) {
                const auto w = ws->getFullscreenWindow();
                if (w != nullptr && ws->m_fullscreenMode != FSMODE_NONE) {
                    // use fakefullscreenstate to preserve client's internal state
                    // fixes youtube fullscreen not restoring properly
                    if (ws->m_fullscreenMode == FSMODE_FULLSCREEN) w->m_wantsInitialFullscreen = true;
                    // we use the getWindowFromHandle function to prevent dangling pointers
                    prevFullscreen.emplace_back(std::make_tuple(PHLWINDOWREF(w), ws->m_fullscreenMode));
                    g_pCompositor->setWindowFullscreenState(w, Desktop::View::SFullscreenState{.internal = FSMODE_NONE, .client = FSMODE_NONE});
                }
            }
        }
    }

    hideRealLayersForOverview();

    active = true;

    // panel offset should be handled by swipe event when swiping
    if (!swiping) {
        *curYOffset = 0;
        curSwipeOffset = (Config::panelHeight + Config::reservedArea) * owner->m_scale;
    }

    updateLayout();
    g_pHyprRenderer->damageMonitor(owner);
    g_pCompositor->scheduleFrameForMonitor(owner);
}

void CHyprspaceWidget::hide() {
    auto owner = getOwner();
    if (!owner) return;

    restoreRealLayersFromOverview();

    // restore fullscreen state
    for (auto& fs : prevFullscreen) {
        const auto w = std::get<0>(fs).lock();
        if (!w) continue;
        const auto oFullscreenMode = std::get<1>(fs);
        g_pCompositor->setWindowFullscreenState(w, Desktop::View::SFullscreenState(oFullscreenMode));
        if (oFullscreenMode == FSMODE_FULLSCREEN) w->m_wantsInitialFullscreen = false;
    }
    prevFullscreen.clear();
    clearOverviewWindowSnapshots();

    active = false;

    // panel offset should be handled by swipe event when swiping
    if (!swiping) {
        *curYOffset = (Config::panelHeight + Config::reservedArea) * owner->m_scale;
        curSwipeOffset = -10.;
    }

    updateLayout();
    g_pHyprRenderer->damageMonitor(owner);
    for (auto& ws : g_pCompositor->getWorkspaces()) {
        if (!ws || ws->m_monitor->m_id != ownerID) continue;
        for (auto& w : g_pCompositor->m_windows) {
            if (!w || w->m_workspace != ws || !w->m_isMapped) continue;
            g_pHyprRenderer->damageWindow(w);
        }
    }
    g_pCompositor->scheduleFrameForMonitor(owner);
}

void CHyprspaceWidget::updateConfig() {
    curAnimationConfig = *::Config::animationTree()->getAnimationPropertyConfig("windows");

    // the fuck is pValues???
    curAnimation = *curAnimationConfig.pValues.lock();
    *curAnimationConfig.pValues.lock() = curAnimation;

    if (Config::overrideAnimSpeed > 0)
        curAnimation.internalSpeed = Config::overrideAnimSpeed;

    g_pAnimationManager->createAnimation(0.F, curYOffset, curAnimationConfig.pValues.lock(), AVARDAMAGE_ENTIRE);
    curYOffset->setCallbackOnEnd([this](auto) {
        if (!active) {
            auto owner = getOwner();
            if (owner) {
                g_pHyprRenderer->damageMonitor(owner);
                for (auto& ws : g_pCompositor->getWorkspaces()) {
                    if (!ws || ws->m_monitor->m_id != ownerID) continue;
                    for (auto& w : g_pCompositor->m_windows) {
                        if (!w || w->m_workspace != ws || !w->m_isMapped) continue;
                        g_pHyprRenderer->damageWindow(w);
                    }
                }
                g_pCompositor->scheduleFrameForMonitor(owner);
            }
        }
    }, false);
    g_pAnimationManager->createAnimation(0.F, workspaceScrollOffset, curAnimationConfig.pValues.lock(), AVARDAMAGE_ENTIRE);
    curYOffset->setValueAndWarp(Config::panelHeight);
    workspaceScrollOffset->setValueAndWarp(0);
}

bool CHyprspaceWidget::isActive() {
    return active;
}
