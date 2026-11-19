#ifndef SAN14_MOD_SDK_ROSTER_VIEW_H
#define SAN14_MOD_SDK_ROSTER_VIEW_H

#include "san14_ui.h"

enum San14RosterViewKind {
    SAN14_ROSTER_VIEW_OFFICER_INFO = 1,
    SAN14_ROSTER_VIEW_OFFICER_SELECTION = 2
};

enum San14RosterViewRowFlags {
    SAN14_ROSTER_ROW_VISIBLE = 1,
    SAN14_ROSTER_ROW_SELECTED = 2,
    SAN14_ROSTER_ROW_DISABLED = 4
};

typedef struct San14RosterViewInfo {
    uint32_t struct_size;
    uint32_t kind;
    San14Handle view;
    uint64_t revision;
    uint64_t generation;
    uint32_t candidate_count;
    uint32_t visible_count;
} San14RosterViewInfo;

typedef struct San14RosterViewRow {
    uint32_t struct_size;
    uint32_t officer_id;
    uint32_t flags;
    uint32_t visible_index; /* UINT32_MAX for hidden rows. */
} San14RosterViewRow;

enum San14RosterHotkeyScope {
    SAN14_ROSTER_KEY_PLANNING_MAP = 1,
    SAN14_ROSTER_KEY_OFFICER_INFO = 2,
    SAN14_ROSTER_KEY_OFFICER_SELECTION = 4
};

/* Return nonzero to consume this press. The Host handles repeats and release. */
typedef int32_t (SAN14_MOD_CALL *San14RosterHotkeyCallback)(void* plugin_context);

typedef struct San14RosterHotkeySpec {
    uint32_t struct_size;
    uint32_t scope_mask;
    uint32_t virtual_key; /* Unmodified F2..F12. */
    uint32_t reserved;
    San14RosterHotkeyCallback callback;
    void* plugin_context;
} San14RosterHotkeySpec;

/* UI thread, active owner, ui.panel + game.read.officers. A view belongs to
 * an open native roster, never to a global roster.capture snapshot. */
typedef struct San14RosterViewApi {
    uint32_t struct_size;
    /* Returns the full candidate set, including filtered rows. With a short
     * buffer returns BUFFER_TOO_SMALL and fills info, without partial rows. */
    San14Result (SAN14_MOD_CALL *capture)(void*, San14RosterViewInfo*, void* rows,
                                         uint32_t row_stride, uint32_t capacity);
    /* A request is a set of unique officer IDs inside the captured candidates.
     * Native selection is preserved, including selected IDs omitted here.
     * Apply and reset compare the last observed revision before any rebuild. */
    San14Result (SAN14_MOD_CALL *apply_projection)(void*, San14Handle view, uint64_t revision,
                                                  const uint32_t* ids, uint32_t count);
    San14Result (SAN14_MOD_CALL *reset)(void*, San14Handle view, uint64_t revision);
    /* Creates an owner-bound, nonmodal filter panel at the supported page
     * position. The Host selects x/y. Add normal ui/extended controls and
     * show through ui. The panel is closed when the native page closes. */
    San14Result (SAN14_MOD_CALL *create_filter_panel)(void*, San14Handle view,
                                                     const San14PanelSpec*, San14PanelHandle*);
    /* Also requires input.hotkey. Conflicts return BUSY. Native dialogs and
     * focused text fields take precedence. */
    San14Result (SAN14_MOD_CALL *register_hotkey)(void*, const San14RosterHotkeySpec*, San14Handle*);
    San14Result (SAN14_MOD_CALL *release_hotkey)(void*, San14Handle);
} San14RosterViewApi;

#endif
