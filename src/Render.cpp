#include "Overview.hpp"
#include "Globals.hpp"
#include <hyprland/src/render/pass/RectPassElement.hpp>
#include <hyprland/src/render/pass/RendererHintsPassElement.hpp>
#include <hyprland/src/render/pass/SurfacePassElement.hpp>
#include <hyprland/src/render/pass/TexPassElement.hpp>
#include <hyprlang.hpp>
#include <hyprutils/utils/ScopeGuard.hpp>


void renderRect(CBox box, CHyprColor color) {
    CRectPassElement::SRectData rectdata;
    rectdata.color = color;
    rectdata.box = box;
    g_pHyprRenderer->m_renderPass.add(makeUnique<CRectPassElement>(rectdata));
}

void renderRectWithBlur(CBox box, CHyprColor color) {
    CRectPassElement::SRectData rectdata;
    rectdata.color = color;
    rectdata.box = box;
    rectdata.blur = true;
    g_pHyprRenderer->m_renderPass.add(makeUnique<CRectPassElement>(rectdata));
}

static CBox insetBox(CBox box, int inset) {
    if (inset <= 0)
        return box;

    box.x += inset;
    box.y += inset;
    box.w -= inset * 2;
    box.h -= inset * 2;
    return box;
}

static CBox pixelSnapBox(CBox box) {
    const double right  = std::round(box.x + box.w);
    const double bottom = std::round(box.y + box.h);

    box.x = std::round(box.x);
    box.y = std::round(box.y);
    box.w = std::max(0.0, right - box.x);
    box.h = std::max(0.0, bottom - box.y);
    return box;
}

void renderBorder(CBox box, CHyprColor color, int size) {
    if (size <= 0 || color.a <= 0.F || box.w <= 0 || box.h <= 0)
        return;

    const double horizontalHeight = std::min<double>(size, box.h);
    const double verticalWidth    = std::min<double>(size, box.w);
    const double verticalHeight   = std::max<double>(box.h - horizontalHeight * 2, 0);

    renderRect({box.x, box.y, box.w, horizontalHeight}, color);
    renderRect({box.x, box.y + box.h - horizontalHeight, box.w, horizontalHeight}, color);

    if (verticalHeight > 0) {
        renderRect({box.x, box.y + horizontalHeight, verticalWidth, verticalHeight}, color);
        renderRect({box.x + box.w - verticalWidth, box.y + horizontalHeight, verticalWidth, verticalHeight}, color);
    }
}

void renderWindowStub(PHLWINDOW pWindow, PHLMONITOR pMonitor, PHLWORKSPACE pWorkspaceOverride, CBox rectOverride, CBox clipBox, const Time::steady_tp& time) {
    if (!pWindow || !pMonitor || !pWorkspaceOverride)
        return;

    if (!pWindow->m_isMapped || !pWindow->wlSurface() || !pWindow->wlSurface()->resource())
        return;

    SRenderModifData renderModif;
    const auto oRealPosition = pWindow->m_realPosition->value();
    const auto oSize         = pWindow->m_realSize->value();

    if (!(oSize.x > 0) || !(pMonitor->m_scale > 0))
        return;

    const float curScaling = rectOverride.w / (oSize.x * pMonitor->m_scale);
    if (!(curScaling > 0) || !(rectOverride.w > 0 && rectOverride.h > 0))
        return;

    renderModif.modifs.push_back(std::make_pair(SRenderModifData::eRenderModifType::RMOD_TYPE_TRANSLATE, std::any((pMonitor->m_position * pMonitor->m_scale) + (rectOverride.pos() / curScaling) - (oRealPosition * pMonitor->m_scale))));
    renderModif.modifs.push_back(std::make_pair(SRenderModifData::eRenderModifType::RMOD_TYPE_SCALE, std::any(curScaling)));
    renderModif.enabled = true;

    g_pHyprRenderer->m_renderPass.add(makeUnique<CRendererHintsPassElement>(CRendererHintsPassElement::SData{renderModif}));
    Hyprutils::Utils::CScopeGuard x([] {
        g_pHyprRenderer->m_renderPass.add(makeUnique<CRendererHintsPassElement>(CRendererHintsPassElement::SData{SRenderModifData{}}));
    });

    g_pHyprRenderer->damageWindow(pWindow);

    CSurfacePassElement::SRenderData renderdata = {pMonitor, time};
    renderdata.pos                              = oRealPosition + pWindow->m_floatingOffset;
    renderdata.w                                = std::max(oSize.x, 5.0);
    renderdata.h                                = std::max(oSize.y, 5.0);
    renderdata.surface                          = pWindow->wlSurface()->resource();
    renderdata.dontRound                        = pWindow->isEffectiveInternalFSMode(FSMODE_FULLSCREEN);
    renderdata.fadeAlpha                        = 1.F;
    renderdata.alpha                            = 1.F;
    renderdata.decorate                         = false;
    renderdata.rounding                         = renderdata.dontRound ? 0 : pWindow->rounding() * curScaling * pMonitor->m_scale;
    renderdata.roundingPower                    = renderdata.dontRound ? 2.0F : pWindow->roundingPower();
    renderdata.blur                             = false;
    renderdata.pWindow                          = pWindow;
    renderdata.clipBox                          = clipBox;
    renderdata.useNearestNeighbor               = false;
    renderdata.squishOversized                  = true;
    renderdata.surfaceCounter                   = 0;

    pWindow->wlSurface()->resource()->breadthfirst(
        [&renderdata, &pWindow](SP<CWLSurfaceResource> surface, const Vector2D& offset, void*) {
            if (!surface || !surface->m_current.texture)
                return;

            if (surface->m_current.size.x < 1 || surface->m_current.size.y < 1)
                return;

            renderdata.localPos    = offset;
            renderdata.texture     = surface->m_current.texture;
            renderdata.surface     = surface;
            renderdata.mainSurface = surface == pWindow->wlSurface()->resource();
            g_pHyprRenderer->m_renderPass.add(makeUnique<CSurfacePassElement>(renderdata));
            renderdata.surfaceCounter++;
        },
        nullptr);
}

void renderLayerStub(PHLLS pLayer, PHLMONITOR pMonitor, CBox rectOverride, const Time::steady_tp& time) {
    if (!g_renderHooksReady || !pRenderLayer || !pLayer || !pMonitor)
        return;

    if (!pLayer->m_mapped || pLayer->m_readyToDelete || !pLayer->m_layerSurface) return;

    Vector2D oRealPosition = pLayer->m_realPosition->value();
    Vector2D oSize = pLayer->m_realSize->value();
    float oAlpha = pLayer->m_alpha->value(); // set to 1 to show hidden top layer
    const auto oFadingOut = pLayer->m_fadingOut;

    if (!(oSize.x > 0))
        return;

    const float curScaling = rectOverride.w / (oSize.x);
    if (!(curScaling > 0))
        return;

    SRenderModifData renderModif;

    renderModif.modifs.push_back(std::make_pair(SRenderModifData::eRenderModifType::RMOD_TYPE_TRANSLATE, std::any(pMonitor->m_position + (rectOverride.pos() / curScaling) - oRealPosition)));
    renderModif.modifs.push_back(std::make_pair(SRenderModifData::eRenderModifType::RMOD_TYPE_SCALE, std::any(curScaling)));
    renderModif.enabled = true;
    pLayer->m_alpha->setValue(1);
    pLayer->m_fadingOut = false;

    g_pHyprRenderer->m_renderPass.add(makeUnique<CRendererHintsPassElement>(CRendererHintsPassElement::SData{renderModif}));
    // remove modif as it goes out of scope (wtf is this blackmagic i need to relearn c++)
    Hyprutils::Utils::CScopeGuard x([] {
        g_pHyprRenderer->m_renderPass.add(makeUnique<CRendererHintsPassElement>(CRendererHintsPassElement::SData{SRenderModifData{}}));
        });

    (*(tRenderLayer)pRenderLayer)(g_pHyprRenderer.get(), pLayer, pMonitor, time, false, false);

    pLayer->m_fadingOut = oFadingOut;
    pLayer->m_alpha->setValue(oAlpha);
}

void renderBackgroundStub(PHLMONITOR pMonitor, CBox rectOverride) {
    if (!g_renderHooksReady || !pRenderBackground || !pMonitor)
        return;

    if (!(pMonitor->m_scale > 0) || !(pMonitor->m_transformedSize.x > 0))
        return;

    const float curScaling = rectOverride.w / pMonitor->m_transformedSize.x;
    if (!(curScaling > 0))
        return;

    SRenderModifData renderModif;
    renderModif.modifs.push_back(std::make_pair(SRenderModifData::eRenderModifType::RMOD_TYPE_TRANSLATE,
        std::any((pMonitor->m_position * pMonitor->m_scale) + (rectOverride.pos() / curScaling) - (pMonitor->m_position * pMonitor->m_scale))));
    renderModif.modifs.push_back(std::make_pair(SRenderModifData::eRenderModifType::RMOD_TYPE_SCALE, std::any(curScaling)));
    renderModif.enabled = true;

    g_pHyprRenderer->m_renderPass.add(makeUnique<CRendererHintsPassElement>(CRendererHintsPassElement::SData{renderModif}));
    Hyprutils::Utils::CScopeGuard x([] {
        g_pHyprRenderer->m_renderPass.add(makeUnique<CRendererHintsPassElement>(CRendererHintsPassElement::SData{SRenderModifData{}}));
    });

    (*(tRenderBackground)pRenderBackground)(g_pHyprRenderer.get(), pMonitor);
}

namespace {
    CBox monitorClipBox(PHLMONITOR monitor) {
        if (!monitor)
            return {};

        return CBox({0, 0}, monitor->m_transformedSize);
    }

    struct SWorkspaceThumbnailScene {
        PHLMONITOR   owner = nullptr;
        PHLWORKSPACE workspace = nullptr;
        CBox         workspaceBox;
        CBox         contentBox;
        double       monitorSizeScaleFactor = 0;
    };

    std::vector<int> getOverviewWorkspaceIDs() {
        return {1, 2, 3, 4, 5};
    }

    void renderWorkspaceLayerGroup(const SWorkspaceThumbnailScene& scene, int layerIndex, const Time::steady_tp& time) {
        if (!scene.owner)
            return;

        for (auto& ls : scene.owner->m_layerSurfaceLayers[layerIndex]) {
            CBox layerBox = pixelSnapBox({
                scene.contentBox.pos() + (ls->m_realPosition->value() - scene.owner->m_position) * scene.monitorSizeScaleFactor,
                ls->m_realSize->value() * scene.monitorSizeScaleFactor,
            });

            g_pHyprRenderer->m_renderData.clipBox = scene.contentBox;
            renderLayerStub(ls.lock(), scene.owner, layerBox, time);
            g_pHyprRenderer->m_renderData.clipBox = monitorClipBox(scene.owner);
        }
    }

    void renderWorkspaceSnapshotTexture(CHyprspaceWidget& widget, const SWorkspaceThumbnailScene& scene) {
        const auto snapshotTexture = widget.getOverviewMonitorSnapshotTexture(scene.workspace);
        if (!snapshotTexture)
            return;

        CTexPassElement::SRenderData textureData;
        textureData.tex          = snapshotTexture;
        textureData.box          = scene.contentBox;
        textureData.a            = 1.F;
        textureData.damage       = g_pHyprRenderer->m_renderData.damage;
        textureData.clipBox      = scene.contentBox;
        textureData.flipEndFrame = true;
        g_pHyprRenderer->m_renderPass.add(makeUnique<CTexPassElement>(textureData));
    }

    std::optional<CBox> getOverviewWindowSourceBox(const SWorkspaceThumbnailScene& scene, PHLWINDOW window) {
        if (!window || !scene.workspace || !scene.owner)
            return std::nullopt;

        return CBox({window->m_realPosition->value(), window->m_realSize->value()});
    }

    std::optional<CBox> getOverviewWindowTargetBox(const SWorkspaceThumbnailScene& scene, const CBox& sourceWindowBox) {
        if (!scene.owner || !(sourceWindowBox.w > 0) || !(sourceWindowBox.h > 0))
            return std::nullopt;

        const double wX = scene.workspaceBox.x + ((sourceWindowBox.x - scene.owner->m_position.x) * scene.monitorSizeScaleFactor * scene.owner->m_scale);
        const double wY = scene.workspaceBox.y + ((sourceWindowBox.y - scene.owner->m_position.y) * scene.monitorSizeScaleFactor * scene.owner->m_scale);
        const double wW = sourceWindowBox.w * scene.monitorSizeScaleFactor * scene.owner->m_scale;
        const double wH = sourceWindowBox.h * scene.monitorSizeScaleFactor * scene.owner->m_scale;

        if (!(wW > 0 && wH > 0))
            return std::nullopt;

        return pixelSnapBox({wX, wY, wW, wH});
    }

    void trackOverviewWindowInputBox(std::vector<std::tuple<PHLWINDOWREF, CBox>>& windowBoxes, PHLMONITOR owner, PHLWINDOW window, CBox targetWindowBox) {
        if (!owner || !window)
            return;

        targetWindowBox.scale(1.0 / owner->m_scale);
        targetWindowBox.x += owner->m_position.x;
        targetWindowBox.y += owner->m_position.y;
        windowBoxes.emplace_back(PHLWINDOWREF(window), targetWindowBox);
    }

    std::optional<CBox> getSnapshotWorkspaceInputSourceBox(CHyprspaceWidget& widget, const SWorkspaceThumbnailScene& scene, PHLWINDOW window) {
        if (!window || !scene.workspace || !scene.owner)
            return std::nullopt;

        // When the active workspace thumbnail is refreshed from the live monitor
        // framebuffer, input boxes must follow the current live geometry too.
        if (widget.isActive() && scene.workspace == scene.owner->m_activeWorkspace)
            return CBox({window->m_realPosition->value(), window->m_realSize->value()});

        return widget.getOverviewWindowSnapshotBox(window, scene.workspace);
    }

    void trackSnapshotWorkspaceWindows(CHyprspaceWidget& widget, const SWorkspaceThumbnailScene& scene, std::vector<std::tuple<PHLWINDOWREF, CBox>>& windowBoxes) {
        if (!scene.workspace || !scene.owner)
            return;

        auto trackWindow = [&](PHLWINDOW window) {
            if (!window)
                return;

            const auto snapshotBox = getSnapshotWorkspaceInputSourceBox(widget, scene, window);
            if (!snapshotBox.has_value())
                return;

            const auto targetWindowBox = getOverviewWindowTargetBox(scene, *snapshotBox);
            if (!targetWindowBox.has_value())
                return;

            trackOverviewWindowInputBox(windowBoxes, scene.owner, window, *targetWindowBox);
        };

        for (auto& window : g_pCompositor->m_windows) {
            if (!window)
                continue;

            if (window->m_workspace == scene.workspace && !window->m_isFloating)
                trackWindow(window);
        }

        for (auto& window : g_pCompositor->m_windows) {
            if (!window)
                continue;

            if (window->m_workspace == scene.workspace && window->m_isFloating && scene.workspace->getLastFocusedWindow() != window)
                trackWindow(window);
        }

        if (scene.workspace->getLastFocusedWindow() && scene.workspace->getLastFocusedWindow()->m_isFloating)
            trackWindow(scene.workspace->getLastFocusedWindow());
    }

    void renderWorkspaceWindows(CHyprspaceWidget& widget, const SWorkspaceThumbnailScene& scene, const Time::steady_tp& time, PHLWINDOW draggedWindow, bool trackInput,
        std::vector<std::tuple<PHLWINDOWREF, CBox>>& windowBoxes) {
        if (!scene.workspace || !scene.owner)
            return;

        auto renderAndTrackWindow = [&](PHLWINDOW window) {
            if (!window || window == draggedWindow)
                return;

            const auto sourceWindowBox = getOverviewWindowSourceBox(scene, window);
            if (!sourceWindowBox.has_value())
                return;

            const auto targetWindowBox = getOverviewWindowTargetBox(scene, *sourceWindowBox);
            if (!targetWindowBox.has_value())
                return;

            g_pHyprRenderer->m_renderData.clipBox = scene.contentBox;
            renderWindowStub(window, scene.owner, scene.workspace, *targetWindowBox, scene.contentBox, time);
            g_pHyprRenderer->m_renderData.clipBox = monitorClipBox(scene.owner);

            if (trackInput)
                trackOverviewWindowInputBox(windowBoxes, scene.owner, window, *targetWindowBox);
        };

        for (auto& window : g_pCompositor->m_windows) {
            if (!window)
                continue;

            if (window->m_workspace == scene.workspace && !window->m_isFloating)
                renderAndTrackWindow(window);
        }

        for (auto& window : g_pCompositor->m_windows) {
            if (!window)
                continue;

            if (window->m_workspace == scene.workspace && window->m_isFloating && scene.workspace->getLastFocusedWindow() != window)
                renderAndTrackWindow(window);
        }

        if (scene.workspace->getLastFocusedWindow() && scene.workspace->getLastFocusedWindow()->m_isFloating)
            renderAndTrackWindow(scene.workspace->getLastFocusedWindow());
    }
}

// NOTE: rects and clipbox positions are relative to the monitor, while damagebox and layers are not, what the fuck? xd
void CHyprspaceWidget::draw() {

    workspaceBoxes.clear();
    windowBoxes.clear();

    PHLWINDOW draggedWindow = draggedWindowRef.lock();

    if (!active && !curYOffset->isBeingAnimated()) return;

    auto owner = getOwner();

    if (!owner) return;

    if (!g_pHyprOpenGL || !g_pHyprRenderer)
        return;
    if (!owner)
        return;

    const auto time = Time::steadyNow();

    owner->m_blurFBShouldRender = true;

    int bottomInvert = 1;
    if (Config::onBottom) bottomInvert = -1;

    // Background box
    CBox widgetBox = {owner->m_position.x, owner->m_position.y + (Config::onBottom * (owner->m_transformedSize.y - ((Config::panelHeight + Config::reservedArea) * owner->m_scale))) - (bottomInvert * curYOffset->value()), owner->m_transformedSize.x, (Config::panelHeight + Config::reservedArea) * owner->m_scale}; //TODO: update size on monitor change

    // set widgetBox relative to current monitor for rendering panel
    widgetBox.x -= owner->m_position.x;
    widgetBox.y -= owner->m_position.y;

    const CBox monitorClip = monitorClipBox(owner);
    g_pHyprRenderer->m_renderData.clipBox = monitorClip;

    // unscaled and relative to owner
    //CBox damageBox = {0, (Config::onBottom * (owner->m_transformedSize.y - ((Config::panelHeight + Config::reservedArea)))) - (bottomInvert * curYOffset->value()), owner->m_transformedSize.x, (Config::panelHeight + Config::reservedArea) * owner->m_scale};

    //owner->addDamage(damageBox);
    g_pHyprRenderer->damageMonitor(owner);
    g_pHyprRenderer->damageMonitor(owner);

    const auto workspaces = getOverviewWorkspaceIDs();
    const int              wsCount    = workspaces.size();

    const double baseMonitorSizeScaleFactor = ((Config::panelHeight - 2 * Config::workspaceMargin) / (owner->m_transformedSize.y)) * owner->m_scale;
    const double baseWorkspaceBoxW          = owner->m_transformedSize.x * baseMonitorSizeScaleFactor;
    const double baseWorkspaceBoxH          = owner->m_transformedSize.y * baseMonitorSizeScaleFactor;
    const double baseWorkspaceGroupWidth    = baseWorkspaceBoxW * wsCount + (Config::workspaceMargin * owner->m_scale) * (wsCount - 1);
    const double workspaceOverflowSize      = std::max<double>(((baseWorkspaceGroupWidth - widgetBox.w) / 2) + (Config::workspaceMargin * owner->m_scale), 0);

    *workspaceScrollOffset = std::clamp<double>(workspaceScrollOffset->goal(), -workspaceOverflowSize, workspaceOverflowSize);

    if (!(baseWorkspaceBoxW > 0 && baseWorkspaceBoxH > 0))
        return;

    auto renderOverviewScene = [&](CBox sceneBox, bool trackInput) -> void {
        if (!(sceneBox.w > 0 && sceneBox.h > 0) || !(owner->m_transformedSize.x > 0))
            return;

        const double sceneScale = sceneBox.w / owner->m_transformedSize.x;
        if (!(sceneScale > 0))
            return;

        const double monitorSizeScaleFactor = baseMonitorSizeScaleFactor * sceneScale;
        const double workspaceBoxW          = baseWorkspaceBoxW * sceneScale;
        const double workspaceBoxH          = baseWorkspaceBoxH * sceneScale;
        const double workspaceGroupWidth    = baseWorkspaceGroupWidth * sceneScale;

        CBox sceneWidgetBox = pixelSnapBox({
            sceneBox.x + widgetBox.x * sceneScale,
            sceneBox.y + widgetBox.y * sceneScale,
            widgetBox.w * sceneScale,
            widgetBox.h * sceneScale,
        });

        double curWorkspaceRectOffsetX = Config::centerAligned ?
            sceneWidgetBox.x + workspaceScrollOffset->value() * sceneScale + (sceneWidgetBox.w / 2.) - (workspaceGroupWidth / 2.) :
            sceneWidgetBox.x + workspaceScrollOffset->value() * sceneScale + Config::workspaceMargin * sceneScale;

        double curWorkspaceRectOffsetY = !Config::onBottom ?
            sceneWidgetBox.y + ((Config::reservedArea + Config::workspaceMargin) * owner->m_scale * sceneScale) :
            sceneWidgetBox.y + sceneWidgetBox.h - ((Config::reservedArea + Config::workspaceMargin) * owner->m_scale * sceneScale) - workspaceBoxH;

        for (auto wsID : workspaces) {
            const auto ws = g_pCompositor->getWorkspaceByID(wsID);
            CBox curWorkspaceBox = pixelSnapBox({curWorkspaceRectOffsetX, curWorkspaceRectOffsetY, workspaceBoxW, workspaceBoxH});
            const auto borderSize = std::max(0, int(std::round(Config::workspaceBorderSize * sceneScale)));
            const auto contentBox = pixelSnapBox(insetBox(curWorkspaceBox, borderSize));
            const SWorkspaceThumbnailScene workspaceScene{
                .owner = owner,
                .workspace = ws,
                .workspaceBox = curWorkspaceBox,
                .contentBox = contentBox,
                .monitorSizeScaleFactor = monitorSizeScaleFactor,
            };

            bool renderedWallpaper = false;
            const bool useMonitorSnapshot = hasOverviewMonitorSnapshot(ws);

            if (ws == owner->m_activeWorkspace && !Config::drawActiveWorkspace) {
                curWorkspaceRectOffsetX += workspaceBoxW + (Config::workspaceMargin * owner->m_scale * sceneScale);
                continue;
            }

            if (useMonitorSnapshot) {
                renderWorkspaceSnapshotTexture(*this, workspaceScene);

                if (trackInput)
                    trackSnapshotWorkspaceWindows(*this, workspaceScene, windowBoxes);
            } else {
                if (contentBox.w > 0 && contentBox.h > 0 && pRenderBackground) {
                    g_pHyprRenderer->m_renderData.clipBox = contentBox;
                    renderBackgroundStub(owner, contentBox);
                    g_pHyprRenderer->m_renderData.clipBox = monitorClip;
                    renderedWallpaper = true;
                }

                if (ws == owner->m_activeWorkspace) {
                    if (!renderedWallpaper && contentBox.w > 0 && contentBox.h > 0 && Config::workspaceActiveBackground.a > 0)
                        renderRect(contentBox, Config::workspaceActiveBackground);
                } else {
                    if (!renderedWallpaper && contentBox.w > 0 && contentBox.h > 0 && Config::workspaceInactiveBackground.a > 0)
                        renderRect(contentBox, Config::workspaceInactiveBackground);
                }

                if (!Config::hideBackgroundLayers) {
                    renderWorkspaceLayerGroup(workspaceScene, 0, time);
                    renderWorkspaceLayerGroup(workspaceScene, 1, time);
                }

                renderWorkspaceWindows(*this, workspaceScene, time, draggedWindow, trackInput, windowBoxes);

                if (owner->m_activeWorkspace != ws || !Config::hideRealLayers) {
                    if (!Config::hideTopLayers)
                        renderWorkspaceLayerGroup(workspaceScene, 2, time);

                    if (!Config::hideOverlayLayers)
                        renderWorkspaceLayerGroup(workspaceScene, 3, time);
                }
            }

            if (ws == owner->m_activeWorkspace)
                renderBorder(curWorkspaceBox, Config::workspaceActiveBorder, borderSize);
            else
                renderBorder(curWorkspaceBox, Config::workspaceInactiveBorder, borderSize);

            if (trackInput) {
                curWorkspaceBox.scale(1 / owner->m_scale);
                curWorkspaceBox.x += owner->m_position.x;
                curWorkspaceBox.y += owner->m_position.y;
                workspaceBoxes.emplace_back(std::make_tuple(wsID, curWorkspaceBox));
            }

            curWorkspaceRectOffsetX += workspaceBoxW + Config::workspaceMargin * owner->m_scale * sceneScale;
        }
    };

    renderOverviewScene(CBox({0, 0}, owner->m_transformedSize), true);
}
