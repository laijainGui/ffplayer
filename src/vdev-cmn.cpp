// 包含头文件
#include "vdev.h"

extern "C" {
#include "libavutil/log.h"
#include "libavutil/time.h"
}

// 内部常量定义
#define COMPLETE_COUNTER  30

// 函数实现
void vdev_pause(void *ctxt, int pause)
{
    VDEV_COMMON_CTXT *c = (VDEV_COMMON_CTXT*)ctxt;
    if (pause) {
        c->status |=  VDEV_PAUSE;
    } else {
        c->status &= ~VDEV_PAUSE;
    }

    // set AV_NOPTS_VALUE to triger re-calculating of
    // start_pts & start_tick in video rendering thread
    c->start_pts = AV_NOPTS_VALUE;
}

void vdev_reset(void *ctxt)
{
    VDEV_COMMON_CTXT *c = (VDEV_COMMON_CTXT*)ctxt;
#if 0 //++ no need to reset vdev buffer queue
    while (0 == sem_trywait(&c->semr)) {
        sem_post(&c->semw);
    }
    c->head   = c->tail =  0;
#endif//-- no need to reset vdev buffer queue
    c->apts   = c->vpts = AV_NOPTS_VALUE;
    c->status = 0;
}

void vdev_getavpts(void *ctxt, int64_t **ppapts, int64_t **ppvpts)
{
    VDEV_COMMON_CTXT *c = (VDEV_COMMON_CTXT*)ctxt;
    if (ppapts) *ppapts = &c->apts;
    if (ppvpts) *ppvpts = &c->vpts;
}

void vdev_setparam(void *ctxt, int id, void *param)
{
    if (!ctxt || !param) return;
    VDEV_COMMON_CTXT *c = (VDEV_COMMON_CTXT*)ctxt;

    switch (id) {
    case PARAM_VDEV_FRAME_RATE:
        c->tickframe = 1000 / (*(int*)param > 1 ? *(int*)param : 1);
        break;
    case PARAM_AVSYNC_TIME_DIFF:
        c->tickavdiff = *(int*)param;
        break;
    }
}

void vdev_getparam(void *ctxt, int id, void *param)
{
    if (!ctxt || !param) return;
    VDEV_COMMON_CTXT *c = (VDEV_COMMON_CTXT*)ctxt;

    switch (id) {
    case PARAM_VDEV_FRAME_RATE:
        *(int*)param = 1000 / c->tickframe;
        break;
    case PARAM_AVSYNC_TIME_DIFF:
        *(int*)param = c->tickavdiff;
        break;
    }
}

void* vdev_create(int type, void *surface, int bufnum, int w, int h, int frate, void *params)
{
    VDEV_COMMON_CTXT *p = (VDEV_COMMON_CTXT*)params;
    VDEV_COMMON_CTXT *c = NULL;

    if (p) {
        surface= p->surface;
        bufnum = p->bufnum;
        w      = p->w;
        h      = p->h;
        frate  = 1000 / p->tickframe;
    }

#ifdef WIN32
    switch (type) {
    case VDEV_RENDER_TYPE_GDI: c = (VDEV_COMMON_CTXT*)vdev_gdi_create(surface, bufnum, w, h, frate); break;
    case VDEV_RENDER_TYPE_D3D: c = (VDEV_COMMON_CTXT*)vdev_d3d_create(surface, bufnum, w, h, frate); break;
    }
#endif
#ifdef ANDROID
    c = (VDEV_COMMON_CTXT*)vdev_android_create(surface, bufnum, w, h, frate);
#endif
    if (c) c->type = type;

    if (p) {
        c->apts       = p->apts;
        c->vpts       = p->vpts;
        c->tickavdiff = p->tickavdiff;
        c->tickframe  = p->tickframe;
        c->ticksleep  = p->ticksleep;
        c->ticklast   = p->ticklast;
        c->status     = p->status;
        vdev_setrect(c, p->x, p->y, w, h);
    }

    return c;
}

void vdev_destroy(void *ctxt)
{
    VDEV_COMMON_CTXT *c = (VDEV_COMMON_CTXT*)ctxt;
#ifdef WIN32
    switch (c->type) {
    case VDEV_RENDER_TYPE_GDI: vdev_gdi_destroy(ctxt); break;
    case VDEV_RENDER_TYPE_D3D: vdev_d3d_destroy(ctxt); break;
    }
#endif
#ifdef ANDROID
    vdev_android_destroy(ctxt);
#endif
}

void vdev_lock(void *ctxt, uint8_t *buffer[8], int linesize[8])
{
    VDEV_COMMON_CTXT *c = (VDEV_COMMON_CTXT*)ctxt;
#ifdef WIN32
    switch (c->type) {
    case VDEV_RENDER_TYPE_GDI: vdev_gdi_lock(ctxt, buffer, linesize); break;
    case VDEV_RENDER_TYPE_D3D: vdev_d3d_lock(ctxt, buffer, linesize); break;
    }
#endif
#ifdef ANDROID
    vdev_android_lock(ctxt, buffer, linesize);
#endif
}

void vdev_unlock(void *ctxt, int64_t pts)
{
    VDEV_COMMON_CTXT *c = (VDEV_COMMON_CTXT*)ctxt;
#ifdef WIN32
    switch (c->type) {
    case VDEV_RENDER_TYPE_GDI: vdev_gdi_unlock(ctxt, pts); break;
    case VDEV_RENDER_TYPE_D3D: vdev_d3d_unlock(ctxt, pts); break;
    }
#endif
#ifdef ANDROID
    vdev_android_unlock(ctxt, pts);
#endif
}

void vdev_setrect(void *ctxt, int x, int y, int w, int h)
{
    VDEV_COMMON_CTXT *c = (VDEV_COMMON_CTXT*)ctxt;
#ifdef WIN32
    switch (c->type) {
    case VDEV_RENDER_TYPE_GDI: vdev_gdi_setrect(ctxt, x, y, w, h); break;
    case VDEV_RENDER_TYPE_D3D: vdev_d3d_setrect(ctxt, x, y, w, h); break;
    }
#endif
#ifdef ANDROID
    vdev_android_setrect(ctxt, x, y, w, h);
#endif
}

void vdev_refresh_background(void *ctxt)
{
    VDEV_COMMON_CTXT *c = (VDEV_COMMON_CTXT*)ctxt;
    RECT rtwin, rect1, rect2, rect3, rect4;
    int  x = c->x, y = c->y, w = c->w, h = c->h;

#ifdef WIN32
    HWND hwnd = (HWND)c->surface;
    GetClientRect(hwnd, &rtwin);
    rect1.left = 0;   rect1.top = 0;   rect1.right = rtwin.right; rect1.bottom = y;
    rect2.left = 0;   rect2.top = y;   rect2.right = x;           rect2.bottom = y+h;
    rect3.left = x+w; rect3.top = y;   rect3.right = rtwin.right; rect3.bottom = y+h;
    rect4.left = 0;   rect4.top = y+h; rect4.right = rtwin.right; rect4.bottom = rtwin.bottom;
    InvalidateRect(hwnd, &rect1, TRUE);
    InvalidateRect(hwnd, &rect2, TRUE);
    InvalidateRect(hwnd, &rect3, TRUE);
    InvalidateRect(hwnd, &rect4, TRUE);
#endif
}

void vdev_handle_complete_and_avsync(void *ctxt)
{
    VDEV_COMMON_CTXT *c = (VDEV_COMMON_CTXT*)ctxt;
    int     tickdiff, scdiff, avdiff = -1;
    int64_t tickcur;

    if (!(c->status & VDEV_PAUSE)) {
        //++ play completed ++//
        if (c->completed_apts != c->apts || c->completed_vpts != c->vpts) {
            c->completed_apts = c->apts;
            c->completed_vpts = c->vpts;
            c->completed_counter = 0;
            c->status &=~VDEV_COMPLETED;
        } else if (++c->completed_counter == COMPLETE_COUNTER) {
            c->status |= VDEV_COMPLETED;
            player_send_message(c->surface, MSG_PLAY_COMPLETED, 0);
        }
        //-- play completed --//

        //++ frame rate & av sync control ++//
        tickcur     = av_gettime_relative() / 1000;
        tickdiff    = (int)(tickcur - c->ticklast);
        c->ticklast = tickcur;

        // re-calculate start_pts & start_tick if needed
        if (c->start_pts == AV_NOPTS_VALUE) {
            c->start_pts = c->vpts;
            c->start_tick= tickcur;
        }

        avdiff = (int)(c->apts - c->vpts - c->tickavdiff); // diff between audio and video pts
        scdiff = (int)(c->start_pts + tickcur - c->start_tick - c->vpts - c->tickavdiff); // diff between system clock and video pts
        if (c->apts <= 0) avdiff = scdiff; // if apts is invalid, sync vpts to system clock

        if (tickdiff - c->tickframe >  5) c->ticksleep--;
        if (tickdiff - c->tickframe < -5) c->ticksleep++;
        if (c->vpts != -1 && c->vpts != AV_NOPTS_VALUE) {
                 if (avdiff >  500) c->ticksleep -= 3;
            else if (avdiff >  50 ) c->ticksleep -= 1;
            else if (avdiff < -500) c->ticksleep += 3;
            else if (avdiff < -50 ) c->ticksleep += 1;
        }
        if (c->ticksleep < 0) c->ticksleep = 0;
        //-- frame rate & av sync control --//
    } else {
        c->ticksleep = c->tickframe;
    }

    if (c->ticksleep > 0) av_usleep(c->ticksleep * 1000);
    av_log(NULL, AV_LOG_INFO, "d: %3d, s: %3d\n", avdiff, c->ticksleep);
}
