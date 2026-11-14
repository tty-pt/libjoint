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
    char *path_copy;
} JointWrapper;

static void JointFinalizer(napi_env env, void* finalize_data, void* finalize_hint) {
    (void)env;
    (void)finalize_hint;
    JointWrapper* wrap = (JointWrapper*)finalize_data;
    if (wrap) {
        if (wrap->is_open && wrap->jd != (unsigned)-1) {
            joint_close(wrap->jd);
            wrap->is_open = false;
        }
        free(wrap->path_copy);
        free(wrap);
    }
}

/* jointInit(optionalFilePath) */
static napi_value JointInit(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value args[1];
    napi_get_cb_info(env, info, &argc, args, NULL, NULL);

    char path_buf[512] = {0};
    if (argc >= 1) {
        size_t len = 0;
        napi_get_value_string_utf8(env, args[0], path_buf, sizeof(path_buf) - 1, &len);
    }

    JointWrapper* wrap = (JointWrapper*)calloc(1, sizeof(JointWrapper));
    if (!wrap) {
        napi_throw_error(env, NULL, "Out of memory");
        return NULL;
    }
    if (path_buf[0]) {
        wrap->path_copy = strdup(path_buf);
    }

    unsigned jd = joint_init(wrap->path_copy);
    if (jd == (unsigned)-1) {
        free(wrap->path_copy);
        free(wrap);
        napi_throw_error(env, NULL, "Failed to initialize libjoint database");
        return NULL;
    }
    /* Burn handle 0 if returned so (void*)(uintptr_t)jd is never NULL for rec_axis_* */
    if (jd == 0 && !wrap->path_copy) {
        unsigned jd2 = joint_init(NULL);
        joint_close(jd);
        jd = jd2;
    }

    wrap->jd = jd;
    wrap->is_open = true;

    napi_value res;
    napi_create_object(env, &res);
    napi_wrap(env, res, wrap, JointFinalizer, NULL, NULL);
    return res;
}

/* jointStart(jointObj, id, startTs) */
static napi_value JointStart(napi_env env, napi_callback_info info) {
    size_t argc = 3;
    napi_value args[3];
    napi_get_cb_info(env, info, &argc, args, NULL, NULL);
    if (argc < 3) {
        napi_throw_type_error(env, NULL, "Expected 3 arguments: (jointObj, id, startTs)");
        return NULL;
    }

    JointWrapper* wrap = NULL;
    napi_unwrap(env, args[0], (void**)&wrap);
    if (!wrap || !wrap->is_open) {
        napi_throw_error(env, NULL, "Joint database is closed or invalid");
        return NULL;
    }

    uint32_t id = 0;
    int64_t start_ts = 0;
    napi_get_value_uint32(env, args[1], &id);
    napi_get_value_int64(env, args[2], &start_ts);

    int rc = joint_start(wrap->jd, (time_t)start_ts, id);
    napi_value res;
    napi_get_boolean(env, rc >= 0, &res);
    return res;
}

/* jointStop(jointObj, id, endTs) */
static napi_value JointStop(napi_env env, napi_callback_info info) {
    size_t argc = 3;
    napi_value args[3];
    napi_get_cb_info(env, info, &argc, args, NULL, NULL);
    if (argc < 3) {
        napi_throw_type_error(env, NULL, "Expected 3 arguments: (jointObj, id, endTs)");
        return NULL;
    }

    JointWrapper* wrap = NULL;
    napi_unwrap(env, args[0], (void**)&wrap);
    if (!wrap || !wrap->is_open) {
        napi_throw_error(env, NULL, "Joint database is closed or invalid");
        return NULL;
    }

    uint32_t id = 0;
    int64_t end_ts = 0;
    napi_get_value_uint32(env, args[1], &id);
    napi_get_value_int64(env, args[2], &end_ts);

    int rc = joint_stop(wrap->jd, (time_t)end_ts, id);
    napi_value res;
    napi_get_boolean(env, rc >= 0, &res);
    return res;
}

/* jointRecord(jointObj, id, startTs[, endTs]) */
static napi_value JointRecord(napi_env env, napi_callback_info info) {
    size_t argc = 4;
    napi_value args[4];
    napi_get_cb_info(env, info, &argc, args, NULL, NULL);
    if (argc < 3) {
        napi_throw_type_error(env, NULL, "Expected 3 or 4 arguments: (jointObj, id, startTs[, endTs])");
        return NULL;
    }

    JointWrapper* wrap = NULL;
    napi_unwrap(env, args[0], (void**)&wrap);
    if (!wrap || !wrap->is_open) {
        napi_throw_error(env, NULL, "Joint database is closed or invalid");
        return NULL;
    }

    uint32_t id = 0;
    int64_t start_ts = 0;
    napi_get_value_uint32(env, args[1], &id);
    napi_get_value_int64(env, args[2], &start_ts);

    bool has_end = false;
    int64_t end_ts = 0;
    if (argc >= 4) {
        napi_valuetype vtype;
        napi_typeof(env, args[3], &vtype);
        if (vtype == napi_number || vtype == napi_bigint) {
            has_end = true;
            napi_get_value_int64(env, args[3], &end_ts);
        }
    }

    joint_start(wrap->jd, (time_t)start_ts, id);
    if (has_end && end_ts > start_ts) {
        joint_stop(wrap->jd, (time_t)end_ts, id);
    }

    napi_value res;
    napi_get_boolean(env, true, &res);
    return res;
}

/* jointErase(jointObj, id) */
static napi_value JointErase(napi_env env, napi_callback_info info) {
    size_t argc = 2;
    napi_value args[2];
    napi_get_cb_info(env, info, &argc, args, NULL, NULL);
    if (argc < 2) {
        napi_throw_type_error(env, NULL, "Expected 2 arguments: (jointObj, id)");
        return NULL;
    }

    JointWrapper* wrap = NULL;
    napi_unwrap(env, args[0], (void**)&wrap);
    if (!wrap || !wrap->is_open) {
        napi_throw_error(env, NULL, "Joint database is closed or invalid");
        return NULL;
    }

    uint32_t id = 0;
    napi_get_value_uint32(env, args[1], &id);

    int rc = joint_erase(wrap->jd, id);
    napi_value res;
    napi_get_boolean(env, rc == 0, &res);
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

    rec_set_t *cands = rec_set_new();
    if (!cands) {
        napi_throw_error(env, NULL, "Out of memory");
        return NULL;
    }

    rec_axis_fill_interval(wrap->jd, (time_t)start_ts, (time_t)end_ts, cands);
    size_t n = rec_set_count(cands);
    const rec_ref_t *refs = rec_set_at(cands);

    napi_value out_arr;
    napi_create_array_with_length(env, n, &out_arr);
    for (size_t i = 0; i < n; i++) {
        napi_value val;
        napi_create_uint32(env, (uint32_t)refs[i], &val);
        napi_set_element(env, out_arr, (uint32_t)i, val);
    }

    rec_set_free(cands);
    return out_arr;
}

/* jointScanTime(dateStr) */
static napi_value JointScanTime(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value args[1];
    napi_get_cb_info(env, info, &argc, args, NULL, NULL);
    if (argc < 1) {
        napi_throw_type_error(env, NULL, "Expected (dateStr)");
        return NULL;
    }

    char buf[128] = {0};
    size_t len = 0;
    napi_get_value_string_utf8(env, args[0], buf, sizeof(buf) - 1, &len);

    time_t ts = sscantime(buf);
    napi_value res;
    napi_create_int64(env, (int64_t)ts, &res);
    return res;
}

/* jointAxisStore(jointObj, ref, specStr) */
static napi_value JointAxisStore(napi_env env, napi_callback_info info) {
    size_t argc = 3;
    napi_value args[3];
    napi_get_cb_info(env, info, &argc, args, NULL, NULL);
    if (argc < 3) {
        napi_throw_type_error(env, NULL, "Expected (jointObj, ref, specStr)");
        return NULL;
    }

    JointWrapper* wrap = NULL;
    napi_unwrap(env, args[0], (void**)&wrap);
    if (!wrap || !wrap->is_open) {
        napi_throw_error(env, NULL, "Joint database is closed or invalid");
        return NULL;
    }

    uint32_t ref = 0;
    napi_get_value_uint32(env, args[1], &ref);

    size_t len = 0;
    napi_get_value_string_utf8(env, args[2], NULL, 0, &len);
    char *val = (char *)malloc(len + 1);
    if (!val) {
        napi_throw_error(env, NULL, "Out of memory");
        return NULL;
    }
    napi_get_value_string_utf8(env, args[2], val, len + 1, &len);

    int rc = rec_axis_store((void *)(uintptr_t)wrap->jd, NULL, (rec_ref_t)ref, val);
    free(val);

    napi_value res;
    napi_get_boolean(env, rc == 0, &res);
    return res;
}

/* jointAxisStoreTyped(jointObj, ref, uint8Array, qtype) */
static napi_value JointAxisStoreTyped(napi_env env, napi_callback_info info) {
    size_t argc = 4;
    napi_value args[4];
    napi_get_cb_info(env, info, &argc, args, NULL, NULL);
    if (argc < 3) {
        napi_throw_type_error(env, NULL, "Expected (jointObj, ref, uint8Array[, qtype])");
        return NULL;
    }

    JointWrapper* wrap = NULL;
    napi_unwrap(env, args[0], (void**)&wrap);
    if (!wrap || !wrap->is_open) {
        napi_throw_error(env, NULL, "Joint database is closed or invalid");
        return NULL;
    }

    uint32_t ref = 0;
    napi_get_value_uint32(env, args[1], &ref);

    bool is_ta = false;
    napi_is_typedarray(env, args[2], &is_ta);
    if (!is_ta) {
        napi_throw_type_error(env, NULL, "Expected TypedArray for binary payload");
        return NULL;
    }

    napi_typedarray_type ta_type;
    size_t byte_len = 0;
    void *data = NULL;
    napi_get_typedarray_info(env, args[2], &ta_type, &byte_len, &data, NULL, NULL);

    uint32_t qtype = 0;
    if (argc >= 4) {
        napi_get_value_uint32(env, args[3], &qtype);
    }

    int rc = rec_axis_store_typed((void *)(uintptr_t)wrap->jd, NULL, (rec_ref_t)ref, data, byte_len, qtype);
    napi_value res;
    napi_get_boolean(env, rc == 0, &res);
    return res;
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
        {"start", NULL, JointStart, NULL, NULL, NULL, napi_default, NULL},
        {"stop", NULL, JointStop, NULL, NULL, NULL, napi_default, NULL},
        {"record", NULL, JointRecord, NULL, NULL, NULL, napi_default, NULL},
        {"erase", NULL, JointErase, NULL, NULL, NULL, napi_default, NULL},
        {"intersect", NULL, JointIntersect, NULL, NULL, NULL, napi_default, NULL},
        {"scanTime", NULL, JointScanTime, NULL, NULL, NULL, napi_default, NULL},
        {"axisStore", NULL, JointAxisStore, NULL, NULL, NULL, napi_default, NULL},
        {"axisStoreTyped", NULL, JointAxisStoreTyped, NULL, NULL, NULL, napi_default, NULL},
        {"close", NULL, JointClose, NULL, NULL, NULL, napi_default, NULL}
    };
    napi_define_properties(env, exports, sizeof(desc) / sizeof(desc[0]), desc);
    return exports;
}

NAPI_MODULE(NODE_GYP_MODULE_NAME, Init)
