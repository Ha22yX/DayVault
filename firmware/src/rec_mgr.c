#include "rec_mgr.h"

static void call(const rec_actions_t *a, void (*fn)(void))
{
    if (a && fn)
        fn();
}

void rec_mgr_init(rec_mgr_t *mgr, const rec_actions_t *actions)
{
    mgr->state = REC_IDLE;
    mgr->actions = actions;
}

rec_state_t rec_mgr_state(const rec_mgr_t *mgr)
{
    return mgr->state;
}

void rec_mgr_event(rec_mgr_t *mgr, rec_event_t evt)
{
    const rec_actions_t *a = mgr->actions;
    switch (mgr->state)
    {
    case REC_IDLE:
        if (evt == REC_EVT_USB_DETACH)
        {
            if (a)
                call(a, a->start_capture);
            mgr->state = REC_RECORDING;
        }
        else if (evt == REC_EVT_USB_ATTACH)
        {
            if (a)
            {
                call(a, a->finalize_file);
                call(a, a->unmount_fs);
            }
            mgr->state = REC_STOPPING;
        }
        break;

    case REC_RECORDING:
        if (evt == REC_EVT_USB_ATTACH)
        {
            if (a)
                call(a, a->stop_capture);
            mgr->state = REC_STOPPING;
        }
        break;

    case REC_STOPPING:
        if (evt == REC_EVT_FINALIZE_DONE)
        {
            if (a)
            {
                call(a, a->finalize_file);
                call(a, a->unmount_fs);
                call(a, a->start_msc);
            }
            mgr->state = REC_MSC;
        }
        break;

    case REC_MSC:
        if (evt == REC_EVT_USB_DETACH)
        {
            if (a)
                call(a, a->stop_msc);
            mgr->state = REC_IDLE;
            if (a)
            {
                call(a, a->mount_fs);
                call(a, a->start_capture);
            }
            mgr->state = REC_RECORDING;
        }
        break;
    }
}
