#ifndef DAYVAULT_POWER_STATE_H
#define DAYVAULT_POWER_STATE_H

typedef enum
{
    PSTATE_BOOT,
    PSTATE_IDLE,
    PSTATE_RECORDING,
    PSTATE_RECORDING_LOW,
    PSTATE_STOPPING,
    PSTATE_STANDBY
} pstate_t;

typedef enum
{
    PEVT_BOOT_OK,
    PEVT_USB_ATTACH,
    PEVT_USB_DETACH,
    PEVT_BATTERY_OK,
    PEVT_BATTERY_WARNING,
    PEVT_BATTERY_CRITICAL,
    PEVT_CARD_FAIL,
    PEVT_WAKEUP
} pevent_t;

typedef struct
{
    void (*on_transition)(pstate_t from, pstate_t to, pevent_t evt);
} pstate_actions_t;

typedef struct
{
    pstate_t state;
    const pstate_actions_t *actions;
} pstate_machine_t;

void pstate_init(pstate_machine_t *m, const pstate_actions_t *actions);
pstate_t pstate_get(const pstate_machine_t *m);
void pstate_handle(pstate_machine_t *m, pevent_t evt);

#endif
