#ifndef REFLECT_GEN_SAMPLE_DBUS_H
#define REFLECT_GEN_SAMPLE_DBUS_H

/**
 * @file reflect_gen_sample_dbus.h
 * @brief Fixture for dbus mount-table generation: owned storage, a
 *        hooked mount (ops), a read-only mount and a nameless event
 *        topic - the four table shapes the emitter supports.
 */

#include <stdint.h>

#include "shrewd-basic/dbus/dbus.h"

extern const shrewd_basic_dbus_ops_t sample_dbus_ops;

/* reflect: type=db_user_t;
   dbus:    type=db_user_t, path=/config/user, name=sample; */
typedef struct {
    int32_t id;
    char name[16];
} db_user_t;

/* reflect: type=db_state_t;
   dbus:    type=db_state_t, path=/hw/adc0, ops=sample_dbus_ops; */
typedef struct {
    int32_t reading;
} db_state_t;

/* reflect: type=db_ro_t;
   dbus:    type=db_ro_t, path=/config/ro, flags=readonly; */
typedef struct {
    uint32_t magic;
} db_ro_t;

/* dbus: path=/event/alarm, flags=event; */

#endif /* REFLECT_GEN_SAMPLE_DBUS_H */
