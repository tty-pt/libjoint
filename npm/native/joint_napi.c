#include <node_api.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>

#include <ttypt/joint.h>

typedef struct {
    unsigned jd;
    bool is_open;
} JointWrapper;

static void JointFinalizer(napi_env env, void* finalize_data, void* finalize_hint) {
    (void)env;
    (void)finalize_hint;
    JointWrapper* wrap = (JointWrapper*)finalize_data;
    if (wrap) {
        if (wrap->is_open && wrap->jd != 0 && wrap->jd != (unsigned)-1) {
            joint_close(wrap->jd);
            wrap->is_open = false;
        }
        free(wrap);
    }
}

/* jointInit(optionalFilePath) */
static napi_value JointInit(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value args[1];
    napi_get_cb_info(env, info, &argc, args, NULL, NULL);

    char path_buf[256] = {0};
    if (argc >= 1) {
        size_t len = 0;
        napi_get_value_string_utf8(env, args[0], path_buf, sizeof(path_buf) - 1, &len);
    }

    unsigned jd = joint_init(path_buf[0] ? path_buf : NULL);
    if (jd == (unsigned)-1) {
        napi_throw_error(env, NULL, "Failed to initialize libjoint database");
        return NULL;
    }

    JointWrapper* wrap = (JointWrapper*)calloc(1, sizeof(JointWrapper));
    if (!wrap) {
        joint_close(jd);
        napi_throw_error(env, NULL, "Out of memory");
        return NULL;
    }
    wrap->jd = jd;
    wrap->is_open = true;

    napi_value res;
    napi_create_object(env, &res);
    napi_wrap(env, res, wrap, JointFinalizer, NULL, NULL);
    return res;
}

/* jointRecord(jointObj, id, startTs, endTs) */
static napi_value JointRecord(napi_env env, napi_callback_info info) {
    size_t argc = 4;
    napi_value args[4];
    napi_get_cb_info(env, info, &argc, args, NULL, NULL);
    if (argc < 4) {
        napi_throw_type_error(env, NULL, "Expected 4 arguments: (jointObj, id, startTs, endTs)");
        return NULL;
    }

    JointWrapper* wrap = NULL;
    napi_unwrap(env, args[0], (void**)&wrap);
    if (!wrap || !wrap->is_open) {
        napi_throw_error(env, NULL, "Joint database is closed or invalid");
        return NULL;
    }

    uint32_t id = 0;
    int64_t start_ts = 0, end_ts = 0;
    napi_get_value_uint32(env, args[1], &id);
    napi_get_value_int64(env, args[2], &start_ts);
    napi_get_value_int64(env, args[3], &end_ts);

    if (end_ts <= start_ts) {
        end_ts = start_ts + 1;
    }

    joint_start(wrap->jd, (time_t)start_ts, id);
    joint_stop(wrap->jd, (time_t)end_ts, id);

    napi_value res;
    napi_get_boolean(env, true, &res);
    return res;
}

/* jointIntersect(jointObj, startTs, endTs) */
static napi_value JointIntersect(napi_env env, napi_callback_info info) {
    size_t argc = 3;
    napi_value args[3];
    napi_get_cb_info(env, info, &argc, args, NULL, NULL);
    if (argc < 3) {
        napi_throw_type_error(env, NULL, "Expected 3 arguments: (jointObj, startTs, endTs)");
        return NULL;
    }

    JointWrapper* wrap = NULL;
    napi_unwrap(env, args[0], (void**)&wrap);
    if (!wrap || !wrap->is_open) {
        napi_throw_error(env, NULL, "Joint database is closed or invalid");
        return NULL;
    }

    int64_t start_ts = 0, end_ts = 0;
    napi_get_value_int64(env, args[1], &start_ts);
    napi_get_value_int64(env, args[2], &end_ts);

    joint_cur_t cur = joint_iter(wrap->jd, (time_t)start_ts, (time_t)end_ts);

    napi_value out_arr;
    napi_create_array(env, &out_arr);

    uint32_t idx = 0;
    time_t min_t, max_t;
    unsigned count, who;
    while (joint_next(&min_t, &max_t, &count, &who, &cur)) {
        napi_value val;
        napi_create_uint32(env, who, &val);
        napi_set_element(env, out_arr, idx++, val);
    }

    return out_arr;
}

/* jointClose(jointObj) */
static napi_value JointClose(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value args[1];
    napi_get_cb_info(env, info, &argc, args, NULL, NULL);
    if (argc < 1) {
        napi_throw_type_error(env, NULL, "Expected (jointObj)");
        return NULL;
    }

    JointWrapper* wrap = NULL;
    napi_unwrap(env, args[0], (void**)&wrap);
    if (wrap && wrap->is_open) {
        joint_close(wrap->jd);
        wrap->is_open = false;
    }

    napi_value res;
    napi_get_boolean(env, true, &res);
    return res;
}

static napi_value Init(napi_env env, napi_value exports) {
    napi_property_descriptor desc[] = {
        {"init", NULL, JointInit, NULL, NULL, NULL, napi_default, NULL},
        {"record", NULL, JointRecord, NULL, NULL, NULL, napi_default, NULL},
        {"intersect", NULL, JointIntersect, NULL, NULL, NULL, napi_default, NULL},
        {"close", NULL, JointClose, NULL, NULL, NULL, napi_default, NULL}
    };
    napi_define_properties(env, exports, sizeof(desc) / sizeof(desc[0]), desc);
    return exports;
}

NAPI_MODULE(NODE_GYP_MODULE_NAME, Init)
