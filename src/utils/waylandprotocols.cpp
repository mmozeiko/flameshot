// Copyright(c) 2017-2018 Alejandro Sirgo Rica & Contributors
//
// This file is part of Flameshot.
//
//     Flameshot is free software: you can redistribute it and/or modify
//     it under the terms of the GNU General Public License as published by
//     the Free Software Foundation, either version 3 of the License, or
//     (at your option) any later version.
//
//     Flameshot is distributed in the hope that it will be useful,
//     but WITHOUT ANY WARRANTY; without even the implied warranty of
//     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//     GNU General Public License for more details.
//
//     You should have received a copy of the GNU General Public License
//     along with Flameshot.  If not, see <http://www.gnu.org/licenses/>.

#include "waylandprotocols.h"

#include "wlr-screencopy-unstable-v1.h"
#include "xdg-output-unstable-v1.h"

#include <QDebug>
#include <QImage>

#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <errno.h>

struct wp_interfaces {
    wl_shm* shm;
    zwlr_screencopy_manager_v1* screencopy_manager;
    wl_output* output;
};

struct buffer {
    wp_interfaces* in;
    struct wl_buffer* wl_buffer;
    void* data;
    wl_shm_format format;
    int width, height, stride;
    bool y_invert;
    bool done;
};

static wl_buffer* create_shm_buffer(wl_shm* shm, wl_shm_format fmt, int width, int height, int stride, void** data_out) {
    int size = stride * height;

    const char shm_name[] = "/flameshot-screencopy";
    int fd = shm_open(shm_name, O_RDWR | O_CREAT | O_EXCL, 0);
    if (fd < 0) {
        qDebug() << "shm_open failed";
        return NULL;
    }
    shm_unlink(shm_name);

    int ret;
    while ((ret = ftruncate(fd, size)) == EINTR) {
    }

    if (ret < 0) {
        close(fd);
        qDebug() << "ftruncate failed";
        return NULL;
    }

    void* data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (data == MAP_FAILED) {
         qDebug() << "mmap failed:" << strerror(errno);
         close(fd);
        return NULL;
    }

    wl_shm_pool* pool = wl_shm_create_pool(shm, fd, size);
    close(fd);
    wl_buffer* buffer = wl_shm_pool_create_buffer(pool, 0, width, height, stride, fmt);
    wl_shm_pool_destroy(pool);

    *data_out = data;
    return buffer;
}

static void frame_handle_buffer(void* data, zwlr_screencopy_frame_v1* frame, uint32_t format, uint32_t width, uint32_t height, uint32_t stride) {
    buffer* buf = (buffer*)data;
    buf->format = (wl_shm_format)format;
    buf->width = width;
    buf->height = height;
    buf->stride = stride;
    buf->wl_buffer = create_shm_buffer(buf->in->shm, buf->format, width, height, stride, &buf->data);
    if (buf->wl_buffer) {
        zwlr_screencopy_frame_v1_copy(frame, buf->wl_buffer);
    } else {
        qDebug() << "failed to create buffer";
        buf->done = true;
    }
}

static void frame_handle_flags(void* data, zwlr_screencopy_frame_v1* frame, uint32_t flags) {
    Q_UNUSED(frame);
    buffer* buf = (buffer*)data;
    buf->y_invert = flags & ZWLR_SCREENCOPY_FRAME_V1_FLAGS_Y_INVERT;
}

static void frame_handle_ready(void* data, zwlr_screencopy_frame_v1* frame, uint32_t tv_sec_hi, uint32_t tv_sec_lo, uint32_t tv_nsec) {
    Q_UNUSED(frame);
    Q_UNUSED(tv_sec_hi);
    Q_UNUSED(tv_sec_lo);
    Q_UNUSED(tv_nsec);
    buffer* buf = (buffer*)data;
    buf->done = true;
}

static void frame_handle_failed(void* data, zwlr_screencopy_frame_v1* frame) {
    Q_UNUSED(frame);
    qDebug() << "failed to copy frame";
    buffer* buf = (buffer*)data;
    buf->done = true;
}

static const zwlr_screencopy_frame_v1_listener frame_listener = {
    frame_handle_buffer,
    frame_handle_flags,
    frame_handle_ready,
    frame_handle_failed,
};

static void handle_global(void* data, struct wl_registry* registry, uint32_t name, const char* interface, uint32_t version) {
    Q_UNUSED(version);
    wp_interfaces* in = (wp_interfaces*)data;
    if (strcmp(interface, wl_output_interface.name) == 0 && in->output == NULL) {
        in->output = (wl_output*)wl_registry_bind(registry, name, &wl_output_interface, 1);
    } else if (strcmp(interface, wl_shm_interface.name) == 0) {
        in->shm = (wl_shm*)wl_registry_bind(registry, name, &wl_shm_interface, 1);
    } else if (strcmp(interface, zwlr_screencopy_manager_v1_interface.name) == 0) {
        in->screencopy_manager = (zwlr_screencopy_manager_v1*)wl_registry_bind(registry, name, &zwlr_screencopy_manager_v1_interface, 1);
    }
}

static void handle_global_remove(void* data, struct wl_registry* registry, uint32_t name) {
    Q_UNUSED(data);
    Q_UNUSED(registry);
    Q_UNUSED(name);
}

static const wl_registry_listener registry_listener = {
    handle_global,
    handle_global_remove,
};

QPixmap WaylandProtocols::CaptureWithScreencopy() {
    QPixmap pixmap;

    wl_display* display = wl_display_connect(NULL);
    if (display == NULL) {
        qDebug() << "failed to create display:" << strerror(errno);
        return pixmap;
    }

    wp_interfaces in = { NULL, NULL, NULL };

    struct wl_registry* registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registry_listener, &in);
    wl_display_dispatch(display);
    wl_display_roundtrip(display);

    if (in.shm == NULL) {
        qDebug() << "compositor is missing wl_shm";
    } else if (in.screencopy_manager == NULL) {
        qDebug() << "compositor doesn't support wlr-screencopy-unstable-v1";
    } else if (in.output == NULL) {
        qDebug() << "no output available";
    } else {
        buffer buf = { &in, NULL, NULL, (wl_shm_format)0, 0, 0, 0, false, false };
        zwlr_screencopy_frame_v1* frame = zwlr_screencopy_manager_v1_capture_output(in.screencopy_manager, 0, in.output);
        zwlr_screencopy_frame_v1_add_listener(frame, &frame_listener, &buf);

        while (!buf.done && wl_display_dispatch(display) != -1) {
        }

        if (buf.wl_buffer) {
            QImage::Format format;

            fprintf(stderr, "%u %u %u\n", buf.width, buf.height, buf.stride);

            switch (buf.format) {
            case WL_SHM_FORMAT_XRGB8888:
            case WL_SHM_FORMAT_XBGR8888:
                format = QImage::Format_RGB32;
                break;
            case WL_SHM_FORMAT_ARGB8888:
            case WL_SHM_FORMAT_ABGR8888:
                format = QImage::Format_ARGB32;
                break;
            default:
                format = QImage::Format_Invalid;
                break;
            }

            if (format != QImage::Format_Invalid) {
                QImage image((const uchar*)buf.data, buf.width, buf.height, buf.stride, format);
                if (buf.y_invert) {
                    image = image.mirrored(false, true);
                }
                if (buf.format == WL_SHM_FORMAT_XBGR8888 || buf.format == WL_SHM_FORMAT_ABGR8888) {
                    image = image.rgbSwapped();
                }

                pixmap = QPixmap::fromImage(image);
            }

            wl_buffer_destroy(buf.wl_buffer);
        }
    }

    wl_display_disconnect(display);

    return pixmap;
}
