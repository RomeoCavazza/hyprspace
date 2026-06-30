#pragma once
#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/render/Framebuffer.hpp>
#include <hyprutils/animation/AnimationConfig.hpp>
#include <optional>
#include "Globals.hpp"

class CHyprspaceWidget {
    struct SWindowGeometrySnapshot {
        PHLWINDOWREF window;
        WORKSPACEID  workspaceID = WORKSPACE_INVALID;
        CBox         box;
    };

    bool active = false;

    int64_t ownerID;

    Hyprutils::Animation::SAnimationPropertyConfig curAnimationConfig;
    Hyprutils::Animation::SAnimationPropertyConfig curAnimation;

    // --- Hit Testing ---
    std::vector<std::tuple<int, CBox>> workspaceBoxes;
    std::vector<std::tuple<PHLWINDOWREF, CBox>> windowBoxes;
    PHLWINDOWREF draggedWindowRef;

    // --- Preserved State ---
    std::vector<std::tuple<PHLWINDOWREF, eFullscreenMode>> prevFullscreen;
    std::vector<std::tuple<PHLLS, float>> oLayerAlpha;

    // --- Overview Snapshots ---
    std::vector<SWindowGeometrySnapshot> overviewWindowSnapshots;

    CFramebuffer overviewMonitorSnapshot;
    bool         overviewMonitorSnapshotValid = false;
    WORKSPACEID  overviewMonitorSnapshotWorkspaceID = WORKSPACE_INVALID;

    std::chrono::system_clock::time_point lastPressedTime = std::chrono::high_resolution_clock::now();

    bool swiping = false;
    bool activeBeforeSwipe = false;
    double avgSwipeSpeed = 0.;
    int swipePoints = 0;
    double curSwipeOffset = 10.;

    PHLANIMVAR<float> workspaceScrollOffset;

    void captureOverviewWindowSnapshots();
    void clearOverviewWindowSnapshots();
    void clearOverviewMonitorSnapshot();
    void hideRealLayersForOverview();
    void restoreRealLayersFromOverview();

public:

    PHLANIMVAR<float> curYOffset;

    CHyprspaceWidget(uint64_t);
    ~CHyprspaceWidget();

    PHLMONITOR getOwner();
    bool isActive();

    void show();
    void hide();

    void updateConfig();

    std::optional<CBox> getOverviewWindowSnapshotBox(PHLWINDOW window, PHLWORKSPACE workspace) const;
    void                captureOverviewMonitorSnapshot(SP<IFramebuffer> sourceFramebuffer, WORKSPACEID workspaceID);
    bool                hasOverviewMonitorSnapshot(PHLWORKSPACE workspace);
    SP<CTexture>        getOverviewMonitorSnapshotTexture(PHLWORKSPACE workspace);

    void draw();

    void updateLayout();

    bool buttonEvent(bool, Vector2D coords);
    bool axisEvent(double, wl_pointer_axis axis, Vector2D coords);

    bool isSwiping();

    bool beginSwipe(IPointer::SSwipeBeginEvent);
    bool updateSwipe(IPointer::SSwipeUpdateEvent);
    bool endSwipe(IPointer::SSwipeEndEvent);

};
