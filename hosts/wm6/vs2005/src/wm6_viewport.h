#ifndef POCKETJS_WM6_VIEWPORT_H
#define POCKETJS_WM6_VIEWPORT_H

/* Shared by both presenters and input; coordinates use an exclusive end. */
typedef struct wm6_viewport {
    int x, y, width, height;
} wm6_viewport;

static __inline wm6_viewport wm6_fit_viewport(
    int width, int height, int logical_width, int logical_height)
{
    wm6_viewport result = {0, 0, 0, 0};
    if (width <= 0 || height <= 0 ||
        logical_width <= 0 || logical_height <= 0)
        return result;
    if (width * logical_height <= height * logical_width) {
        result.width = width;
        result.height = logical_height * width / logical_width;
    } else {
        result.height = height;
        result.width = logical_width * height / logical_height;
    }
    result.x = (width - result.width) / 2;
    result.y = (height - result.height) / 2;
    return result;
}

static __inline int wm6_map_touch(
    wm6_viewport viewport, int logical_width, int logical_height,
    int captured, int *x, int *y)
{
    if (viewport.width <= 0 || viewport.height <= 0)
        return 0;
    *x -= viewport.x;
    *y -= viewport.y;
    if (!captured && (*x < 0 || *y < 0 ||
        *x >= viewport.width || *y >= viewport.height))
        return 0;
    if (*x < 0) *x = 0;
    if (*y < 0) *y = 0;
    if (*x >= viewport.width) *x = viewport.width - 1;
    if (*y >= viewport.height) *y = viewport.height - 1;
    *x = *x * logical_width / viewport.width;
    *y = *y * logical_height / viewport.height;
    return 1;
}

#endif
