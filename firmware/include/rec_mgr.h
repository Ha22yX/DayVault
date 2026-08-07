#ifndef DAYVAULT_REC_MGR_H
#define DAYVAULT_REC_MGR_H

typedef enum
{
    REC_IDLE = 0,
    REC_RECORDING,
    REC_STOPPING,
    REC_MSC
} rec_state_t;

typedef enum
{
    REC_EVT_USB_ATTACH = 0,
    REC_EVT_USB_DETACH,
    REC_EVT_CAPTURE_STARTED,
    REC_EVT_FINALIZE_DONE,
    REC_EVT_MSC_READY,
    REC_EVT_MSC_DONE
} rec_event_t;

typedef struct
{
    void (*start_capture)(void);
    void (*stop_capture)(void);
    void (*finalize_file)(void);
    void (*mount_fs)(void);
    void (*unmount_fs)(void);
    void (*start_msc)(void);
    void (*stop_msc)(void);
} rec_actions_t;

typedef struct
{
    rec_state_t state;
    const rec_actions_t *actions;
} rec_mgr_t;

void rec_mgr_init(rec_mgr_t *mgr, const rec_actions_t *actions);
void rec_mgr_event(rec_mgr_t *mgr, rec_event_t evt);
rec_state_t rec_mgr_state(const rec_mgr_t *mgr);

#endif
