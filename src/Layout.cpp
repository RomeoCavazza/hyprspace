#include "Overview.hpp"
#include "Globals.hpp"

static ::Config::CCssGapData getOverviewGapsOut(const ::Config::CCssGapData& baseGapsOut, int panelOffset) {
    auto gapsOut = baseGapsOut;

    if (Config::onBottom)
        gapsOut.m_bottom += panelOffset;
    else
        gapsOut.m_top += panelOffset;

    return gapsOut;
}

// FIXME: preserve original workspace rules
void CHyprspaceWidget::updateLayout() {
    const auto currentHeight = Config::panelHeight + Config::reservedArea;
    const auto pMonitor = getOwner();
    if (!pMonitor) return;

    if (g_configReloading) {
        pMonitor->m_reservedArea = Desktop::CReservedArea();
        g_pHyprRenderer->arrangeLayersForMonitor(ownerID);
        g_layoutManager->invalidateMonitorGeometries(pMonitor);
        return;
    }

    static auto PGAPSINDATA = CConfigValue<::Config::IComplexConfigValue>("general:gaps_in");
    static auto PGAPSOUTDATA = CConfigValue<::Config::IComplexConfigValue>("general:gaps_out");

    auto* const PGAPSIN = dynamic_cast<::Config::CCssGapData*>(PGAPSINDATA.ptr());
    auto* const PGAPSOUT = dynamic_cast<::Config::CCssGapData*>(PGAPSOUTDATA.ptr());
    if (!PGAPSIN || !PGAPSOUT)
        return;

    const auto resetWorkspaceRule = [&](const PHLWORKSPACE& ws) {
        const auto curRules = std::to_string(ws->m_id) + ", gapsin:" + PGAPSIN->toString() + ", gapsout:" + PGAPSOUT->toString();
        HyprspaceCompat::handleWorkspaceRules("", curRules);
    };

    // Never use monitor reserved areas for overview layout. In Hyprland they are
    // monitor-scoped, which propagates the push effect beyond the currently
    // visible workspace. We emulate the strut with workspace-specific gaps
    // instead so only the targeted workspace is affected.
    pMonitor->m_reservedArea = Desktop::CReservedArea();
    g_pHyprRenderer->arrangeLayersForMonitor(ownerID);

    // Reset every workspace rule we may have touched on this monitor so the
    // overview effect follows the currently active workspace instead of sticking
    // to previously active ones.
    for (auto& ws : g_pCompositor->getWorkspaces()) {
        if (!ws || ws->m_monitor->m_id != ownerID || ws->m_isSpecialWorkspace)
            continue;

        resetWorkspaceRule(ws.lock());
    }

    if (Config::affectStrut && active && pMonitor->m_activeWorkspace && !pMonitor->m_activeWorkspace->m_isSpecialWorkspace) {
        auto gapsIn = Config::overrideGaps ? ::Config::CCssGapData(Config::gapsIn) : *PGAPSIN;
        auto gapsOutBase = Config::overrideGaps ? ::Config::CCssGapData(Config::gapsOut) : *PGAPSOUT;
        auto gapsOut = getOverviewGapsOut(gapsOutBase, currentHeight);

        const auto curRules = std::to_string(pMonitor->activeWorkspaceID()) + ", gapsin:" + gapsIn.toString() + ", gapsout:" + gapsOut.toString();
        HyprspaceCompat::handleWorkspaceRules("", curRules);
    }

    g_layoutManager->invalidateMonitorGeometries(pMonitor);
}
