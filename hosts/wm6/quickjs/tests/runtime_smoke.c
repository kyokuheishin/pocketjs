#include <assert.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <windows.h>

#include "wm6_quickjs_abi.h"
#include "../../vs2005/src/wm6_viewport.h"

unsigned int wm6_qjs_abi_version(void);
wm6_qjs_handle wm6_qjs_create(
    unsigned int memory_limit,
    unsigned int stack_limit,
    unsigned int viewport_width,
    unsigned int viewport_height,
    char *error,
    unsigned int error_capacity);
int wm6_qjs_set_pak(
    wm6_qjs_handle handle,
    const unsigned char *data,
    unsigned int data_length,
    char *error,
    unsigned int error_capacity);
int wm6_qjs_eval(
    wm6_qjs_handle handle,
    const char *source,
    unsigned int source_length,
    char *output,
    unsigned int output_capacity);
int wm6_qjs_drain_jobs(
    wm6_qjs_handle handle,
    char *output,
    unsigned int output_capacity);
const unsigned char *wm6_qjs_frame(
    wm6_qjs_handle handle,
    unsigned int buttons,
    const unsigned int *touches,
    unsigned int touch_count,
    unsigned int *width,
    unsigned int *height,
    unsigned int *stride,
    unsigned int *byte_length,
    char *error,
    unsigned int error_capacity);
void wm6_qjs_destroy(wm6_qjs_handle handle);

static unsigned char *read_file(const char *path, unsigned int *length)
{
    FILE *file;
    long raw_length;
    unsigned char *bytes;

    *length = 0;
    file = fopen(path, "rb");
    if (!file)
        return NULL;
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return NULL;
    }
    raw_length = ftell(file);
    if (raw_length < 0 || (unsigned long)raw_length > 0xffffffffUL ||
        fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }
    bytes = (unsigned char *)malloc(
        raw_length > 0 ? (size_t)raw_length : 1u);
    if (!bytes ||
        fread(bytes, 1, (size_t)raw_length, file) !=
            (size_t)raw_length) {
        free(bytes);
        fclose(file);
        return NULL;
    }
    fclose(file);
    *length = (unsigned int)raw_length;
    return bytes;
}

static uint64_t hash_bytes(const unsigned char *bytes, unsigned int length)
{
    uint64_t hash;
    unsigned int index;

    hash = UINT64_C(0xcbf29ce484222325);
    for (index = 0; index < length; index++) {
        hash ^= bytes[index];
        hash *= UINT64_C(0x100000001b3);
    }
    return hash;
}

static int parse_dimension(const char *text, unsigned int *value)
{
    char *end;
    unsigned long parsed;

    if (!text || !text[0])
        return 0;
    end = NULL;
    parsed = strtoul(text, &end, 10);
    if (!end || *end != '\0' || parsed == 0 || parsed > 1024)
        return 0;
    *value = (unsigned int)parsed;
    return 1;
}

static int fail(
    const char *step,
    const char *detail,
    wm6_qjs_handle runtime,
    unsigned char *bundle,
    unsigned char *pak)
{
    fprintf(stderr, "%s failed: %s\n", step, detail ? detail : "");
    if (runtime)
        wm6_qjs_destroy(runtime);
    free(bundle);
    free(pak);
    return 1;
}

static void check_viewport(void)
{
    wm6_viewport v;
    int x, y;

    v = wm6_fit_viewport(320, 240, 480, 272);
    assert(v.x == 0 && v.y == 29 && v.width == 320 && v.height == 181);
    x = 160; y = 119;
    assert(wm6_map_touch(v, 480, 272, 0, &x, &y) && x == 240 && y == 135);
    x = 160; y = 28;
    assert(!wm6_map_touch(v, 480, 272, 0, &x, &y));
    x = 160; y = 210;
    assert(!wm6_map_touch(v, 480, 272, 0, &x, &y));
    x = 320; y = 120;
    assert(!wm6_map_touch(v, 480, 272, 0, &x, &y));
    x = -20; y = 260;
    assert(wm6_map_touch(v, 480, 272, 1, &x, &y) && x == 0 && y == 270);
    v = wm6_fit_viewport(640, 480, 480, 272);
    assert(v.x == 0 && v.y == 59 && v.width == 640 && v.height == 362);
    v = wm6_fit_viewport(240, 320, 480, 272);
    assert(v.x == 0 && v.y == 92 && v.width == 240 && v.height == 136);
    v = wm6_fit_viewport(960, 272, 480, 272);
    assert(v.x == 240 && v.y == 0 && v.width == 480 && v.height == 272);
    v = wm6_fit_viewport(0, 0, 480, 272);
    x = 0; y = 0;
    assert(!wm6_map_touch(v, 480, 272, 0, &x, &y));
}

static const char test_transport[] =
    "globalThis.__wm6Tree = null; globalThis.__wm6Request = '';"
    "globalThis.__pocketDevtoolsTransport = {"
    "send: function(line) { var m = JSON.parse(line);"
    "if (m.t === 'tree') __wm6Tree = m.root; },"
    "recv: function() { var q = __wm6Request; __wm6Request = ''; return q; }};"
    "globalThis.__wm6Find = function(n, name) {"
    "if (!n) return null; if (n.n === name) return n;"
    "for (var c of n.k || []) { var found = __wm6Find(c, name);"
    "if (found) return found; } return null; };"
    "globalThis.__wm6Texts = function(n) { return n ?"
    "(n.x || '') + (n.k || []).map(__wm6Texts).join('') : ''; };";

static int check_cards(wm6_qjs_handle runtime, char *message, unsigned int capacity)
{
    static const struct { unsigned int buttons; const char *detail; } steps[] = {
        {0x0020u, ""}, {0x2000u, "Layout"}, {0x2000u, ""},
        {0x0020u, ""}, {0x2000u, "Motion"}, {0x0020u, "Motion"},
        {0x2000u, "Input"}, {0x0080u, "Input"},
        {0x2000u, "Motion"}, {0x2000u, ""}
    };
    static const char request[] = "__wm6Request = '{\"t\":\"getTree\"}'";
    static const char detail[] = "__wm6Texts(__wm6Find(__wm6Tree, 'Detail'))";
    static const char screen[] = "!!__wm6Find(__wm6Tree, 'CardsScreen')";
    unsigned int i;
    unsigned int touch;

    if (wm6_qjs_eval(runtime, screen, sizeof(screen) - 1, message, capacity) ||
        strcmp(message, "true") != 0)
        return 0;
    for (i = 0; i < sizeof(steps) / sizeof(steps[0]); i++) {
        if (!wm6_qjs_frame(runtime, steps[i].buttons, NULL, 0,
                NULL, NULL, NULL, NULL, message, capacity) ||
            wm6_qjs_eval(runtime, request, sizeof(request) - 1, message, capacity) ||
            !wm6_qjs_frame(runtime, 0, NULL, 0,
                NULL, NULL, NULL, NULL, message, capacity) ||
            wm6_qjs_eval(runtime, detail, sizeof(detail) - 1, message, capacity))
            return 0;
        if (steps[i].detail[0] ? !strstr(message, steps[i].detail) : message[0] != 0) {
            fprintf(stderr, "Cards step %u expected detail '%s', got '%s'\n",
                i, steps[i].detail, message);
            return 0;
        }
    }
    /* A logical touch on the first card must open Layout exactly once. */
    touch = 0x80000000u | (110u << 10) | 80u;
    if (!wm6_qjs_frame(runtime, 0, &touch, 1,
            NULL, NULL, NULL, NULL, message, capacity) ||
        !wm6_qjs_frame(runtime, 0, &touch, 1,
            NULL, NULL, NULL, NULL, message, capacity) ||
        !wm6_qjs_frame(runtime, 0, NULL, 0,
            NULL, NULL, NULL, NULL, message, capacity) ||
        wm6_qjs_eval(runtime, request, sizeof(request) - 1, message, capacity) ||
        !wm6_qjs_frame(runtime, 0, NULL, 0,
            NULL, NULL, NULL, NULL, message, capacity) ||
        wm6_qjs_eval(runtime, detail, sizeof(detail) - 1, message, capacity))
        return 0;
    return strstr(message, "Layout") != NULL;
}

int main(int argument_count, char **arguments)
{
    unsigned char *bundle;
    unsigned char *pak;
    unsigned int bundle_length;
    unsigned int pak_length;
    wm6_qjs_handle runtime;
    char message[512];
    const unsigned char *pixels;
    unsigned int width;
    unsigned int height;
    unsigned int stride;
    unsigned int byte_length;
    unsigned int index;
    unsigned int nonzero_alpha;
    unsigned int changed_pixels;
    unsigned int viewport_width;
    unsigned int viewport_height;
    unsigned int expected_stride;
    unsigned int expected_length;
    uint64_t hash;

    check_viewport();
    viewport_width = 480u;
    viewport_height = 272u;
    if (argument_count != 3 && argument_count != 5) {
        fprintf(
            stderr,
            "usage: runtime_smoke BUNDLE PAK [WIDTH HEIGHT]\n");
        return 2;
    }
    if (argument_count == 5 &&
        (!parse_dimension(arguments[3], &viewport_width) ||
         !parse_dimension(arguments[4], &viewport_height))) {
        fprintf(stderr, "viewport must be 1..1024 pixels per axis\n");
        return 2;
    }
    if (wm6_qjs_abi_version() != WM6_QJS_ABI_VERSION) {
        fprintf(stderr, "unexpected WM6 runtime ABI\n");
        return 1;
    }
    bundle = read_file(arguments[1], &bundle_length);
    pak = read_file(arguments[2], &pak_length);
    if (!bundle || !pak)
        return fail("reading Cards assets", "", NULL, bundle, pak);

    runtime = wm6_qjs_create(
        8u * 1024u * 1024u,
        256u * 1024u,
        viewport_width,
        viewport_height,
        message,
        sizeof(message));
    if (!runtime)
        return fail("runtime creation", message, NULL, bundle, pak);
    if (wm6_qjs_set_pak(
            runtime, pak, pak_length, message, sizeof(message)) != 0)
        return fail("PAK installation", message, runtime, bundle, pak);
    if (wm6_qjs_eval(runtime, test_transport, sizeof(test_transport) - 1,
            message, sizeof(message)) != 0)
        return fail("test transport", message, runtime, bundle, pak);
    if (wm6_qjs_eval(
            runtime,
            (const char *)bundle,
            bundle_length,
            message,
            sizeof(message)) != 0)
        return fail("Cards evaluation", message, runtime, bundle, pak);
    if (wm6_qjs_drain_jobs(runtime, message, sizeof(message)) < 0)
        return fail("initial job drain", message, runtime, bundle, pak);

    pixels = wm6_qjs_frame(
        runtime,
        0,
        NULL,
        0,
        &width,
        &height,
        &stride,
        &byte_length,
        message,
        sizeof(message));
    if (!pixels)
        return fail("first frame", message, runtime, bundle, pak);
    expected_stride = viewport_width * 4u;
    expected_length = expected_stride * viewport_height;
    if (width != viewport_width || height != viewport_height ||
        stride != expected_stride || byte_length != expected_length) {
        snprintf(
            message,
            sizeof(message),
            "expected %ux%u stride=%u bytes=%u ARGB32",
            viewport_width,
            viewport_height,
            expected_stride,
            expected_length);
        return fail(
            "frame geometry", message, runtime, bundle, pak);
    }

    nonzero_alpha = 0;
    changed_pixels = 0;
    for (index = 3; index < byte_length; index += 4) {
        if (pixels[index] != 0) {
            nonzero_alpha++;
        }
        if (pixels[index - 3] != pixels[0] ||
            pixels[index - 2] != pixels[1] ||
            pixels[index - 1] != pixels[2])
            changed_pixels++;
        if (nonzero_alpha >= 16 && changed_pixels >= 16)
            break;
    }
    if (nonzero_alpha < 16)
        return fail(
            "frame contents", "framebuffer is transparent", runtime, bundle, pak);
    if (changed_pixels < 16)
        return fail(
            "frame contents", "framebuffer is a flat color", runtime, bundle, pak);
    hash = hash_bytes(pixels, byte_length);
    for (index = 0; index < 60; index++) {
        pixels = wm6_qjs_frame(runtime, 0, NULL, 0,
            NULL, NULL, NULL, NULL, message, sizeof(message));
        if (!pixels)
            return fail("animation frame", message, runtime, bundle, pak);
    }
    if (hash_bytes(pixels, byte_length) == hash)
        return fail("Cards animation", "pixels did not change", runtime, bundle, pak);
    if (!check_cards(runtime, message, sizeof(message)))
        return fail("Cards interaction", message, runtime, bundle, pak);

    wm6_qjs_destroy(runtime);
    free(bundle);
    free(pak);
    printf(
        "WM6 native runtime smoke passed: %ux%u stride=%u bytes=%u "
        "fnv1a=%08x%08x\n",
        width,
        height,
        stride,
        byte_length,
        (unsigned int)(hash >> 32),
        (unsigned int)hash);
    return 0;
}
