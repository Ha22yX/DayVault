#include "power_state.h"

void pstate_init(pstate_machine_t *m, const pstate_actions_t *actions)
{
    m->state = PSTATE_BOOT;
    m->actions = actions;
}

pstate_t pstate_get(const pstate_machine_t *m)
{
    return m->state;
}

static void go(pstate_machine_t *m, pstate_t to, pevent_t evt)
{
    if (to == m->state)
        return;
    if (m->actions && m->actions->on_transition)
        m->actions->on_transition(m->state, to, evt);
    m->state = to;
}

void pstate_handle(pstate_machine_t *m, pevent_t evt)
{
    switch (m->state)
    {
    case PSTATE_BOOT:
        if (evt == PEVT_USB_ATTACH)      go(m, PSTATE_IDLE, evt);
        else if (evt == PEVT_BATTERY_CRITICAL) go(m, PSTATE_STOPPING, evt);
        else if (evt == PEVT_BOOT_OK)    go(m, PSTATE_RECORDING, evt);
        break;
    case PSTATE_IDLE:
        if (evt == PEVT_USB_DETACH)      go(m, PSTATE_RECORDING, evt);
        break;
    case PSTATE_RECORDING:
        if (evt == PEVT_USB_ATTACH)      go(m, PSTATE_IDLE, evt);
        else if (evt == PEVT_BATTERY_WARNING) go(m, PSTATE_RECORDING_LOW, evt);
        else if (evt == PEVT_BATTERY_CRITICAL) go(m, PSTATE_STOPPING, evt);
        else if (evt == PEVT_CARD_FAIL)  go(m, PSTATE_STOPPING, evt);
        break;
    case PSTATE_RECORDING_LOW:
        if (evt == PEVT_USB_ATTACH)      go(m, PSTATE_IDLE, evt);
        else if (evt == PEVT_BATTERY_OK) go(m, PSTATE_RECORDING, evt);
        else if (evt == PEVT_BATTERY_CRITICAL) go(m, PSTATE_STOPPING, evt);
        else if (evt == PEVT_CARD_FAIL)  go(m, PSTATE_STOPPING, evt);
        break;
    case PSTATE_STOPPING:
        go(m, PSTATE_STANDBY, evt);   /* stopping completes on any event */
        break;
    case PSTATE_STANDBY:
        if (evt == PEVT_WAKEUP)        go(m, PSTATE_BOOT, evt);
        break;
    }
}
