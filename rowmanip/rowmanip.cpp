#include <stdlib.h>
#include "VapourSynth4.h"
#include "VSHelper4.h"

typedef struct {
    VSNode *node;
    int interval;
} SeparateRowsData;

typedef struct {
    VSNode *node;
    int period;
} WeaveRowsData;

template<typename T>
static void separate_rows_process_frame(const T* srcp, T* dstp, int plane_width, int new_plane_height,
    int interval, int row_offset, ptrdiff_t src_stride, ptrdiff_t dst_stride) {
    for (int y = 0; y < new_plane_height; y++) {
        const T* src_row = srcp + (y * interval + row_offset) * src_stride / sizeof(T);
        T* dst_row = dstp + y * dst_stride / sizeof(T);
        memcpy(dst_row, src_row, plane_width * sizeof(T));
    }
}

template<typename T>
static void weave_rows_process_frame(const T* srcp, T* dstp, int plane_width, int src_plane_height,
    int period, int row_idx, ptrdiff_t src_stride, ptrdiff_t dst_stride) {
    for (int y = 0; y < src_plane_height; y++) {
        const T* src_row = srcp + y * src_stride / sizeof(T);
        T* dst_row = dstp + (y * period + row_idx) * dst_stride / sizeof(T);
        memcpy(dst_row, src_row, plane_width * sizeof(T));
    }
}

static const VSFrame* VS_CC separateRowsGetFrame(int n, int activationReason, void* instanceData,
    void** frameData, VSFrameContext* frameCtx, VSCore* core, const VSAPI* vsapi) {
    SeparateRowsData* d = (SeparateRowsData*)instanceData;

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n / d->interval, d->node, frameCtx);
    }
    else if (activationReason == arAllFramesReady) {
        const VSFrame* src = vsapi->getFrameFilter(n / d->interval, d->node, frameCtx);
        const VSVideoFormat* fi = vsapi->getVideoFrameFormat(src);
        int height = vsapi->getFrameHeight(src, 0);
        int width = vsapi->getFrameWidth(src, 0);
        int new_height = height / d->interval;

        VSFrame* dst = vsapi->newVideoFrame(fi, width, new_height, src, core);

        for (int plane = 0; plane < fi->numPlanes; plane++) {
            const uint8_t* srcp = vsapi->getReadPtr(src, plane);
            ptrdiff_t src_stride = vsapi->getStride(src, plane);
            uint8_t* dstp = vsapi->getWritePtr(dst, plane);
            ptrdiff_t dst_stride = vsapi->getStride(dst, plane);
            int plane_height = vsapi->getFrameHeight(src, plane);
            int plane_width = vsapi->getFrameWidth(src, plane);
            int new_plane_height = plane_height / d->interval;
            int row_offset = (n % d->interval);

            if (fi->bitsPerSample <= 8)
                separate_rows_process_frame<uint8_t>(srcp, dstp, plane_width, new_plane_height,
                    d->interval, row_offset, src_stride, dst_stride);
            else if (fi->bitsPerSample <= 16)
                separate_rows_process_frame<uint16_t>((const uint16_t*)srcp, (uint16_t*)dstp,
                    plane_width, new_plane_height, d->interval, row_offset, src_stride, dst_stride);
            else
                separate_rows_process_frame<float>((const float*)srcp, (float*)dstp,
                    plane_width, new_plane_height, d->interval, row_offset, src_stride, dst_stride);
        }

        vsapi->freeFrame(src);
        return dst;
    }

    return nullptr;

    (void) frameData;
}

static const VSFrame* VS_CC weaveRowsGetFrame(int n, int activationReason, void* instanceData,
    void** frameData, VSFrameContext* frameCtx, VSCore* core, const VSAPI* vsapi) {
    WeaveRowsData* d = (WeaveRowsData*)instanceData;

    if (activationReason == arInitial) {
        for (int i = 0; i < d->period; i++) {
            int src_frame = n * d->period + i;
            if (src_frame < vsapi->getVideoInfo(d->node)->numFrames) {
                vsapi->requestFrameFilter(src_frame, d->node, frameCtx);
            }
        }
    }
    else if (activationReason == arAllFramesReady) {
        int start_frame = n * d->period;
        const VSVideoInfo* vi = vsapi->getVideoInfo(d->node);
        
        if (start_frame + d->period > vi->numFrames) {
            vsapi->setFilterError("WeaveRows: Not enough frames in input clip", frameCtx);
            return nullptr;
        }

        const VSFrame* first_frame = vsapi->getFrameFilter(start_frame, d->node, frameCtx);
        const VSVideoFormat* fi = vsapi->getVideoFrameFormat(first_frame);
        int src_height = vsapi->getFrameHeight(first_frame, 0);
        int width = vsapi->getFrameWidth(first_frame, 0);
        int dst_height = src_height * d->period;

        VSFrame* dst = vsapi->newVideoFrame(fi, width, dst_height, first_frame, core);
        vsapi->freeFrame(first_frame);

        for (int plane = 0; plane < fi->numPlanes; plane++) {
            uint8_t* dstp = vsapi->getWritePtr(dst, plane);
            ptrdiff_t dst_stride = vsapi->getStride(dst, plane);
            int plane_width = vsapi->getFrameWidth(dst, plane);

            for (int i = 0; i < d->period; i++) {
                const VSFrame* src = vsapi->getFrameFilter(start_frame + i, d->node, frameCtx);
                const uint8_t* srcp = vsapi->getReadPtr(src, plane);
                ptrdiff_t src_stride = vsapi->getStride(src, plane);
                int src_plane_height = vsapi->getFrameHeight(src, plane);

                if (fi->bitsPerSample <= 8)
                    weave_rows_process_frame<uint8_t>(srcp, dstp, plane_width, src_plane_height,
                        d->period, i, src_stride, dst_stride);
                else if (fi->bitsPerSample <= 16)
                    weave_rows_process_frame<uint16_t>((const uint16_t*)srcp, (uint16_t*)dstp,
                        plane_width, src_plane_height, d->period, i, src_stride, dst_stride);
                else
                    weave_rows_process_frame<float>((const float*)srcp, (float*)dstp,
                        plane_width, src_plane_height, d->period, i, src_stride, dst_stride);

                vsapi->freeFrame(src);
            }
        }

        return dst;
    }

    return nullptr;

    (void) frameData;
}

static void VS_CC separateRowsFree(void* instanceData, VSCore* core, const VSAPI* vsapi) {
    SeparateRowsData* d = (SeparateRowsData*)instanceData;
    vsapi->freeNode(d->node);
    free(d);

    (void) core;
}

static void VS_CC weaveRowsFree(void* instanceData, VSCore* core, const VSAPI* vsapi) {
    WeaveRowsData* d = (WeaveRowsData*)instanceData;
    vsapi->freeNode(d->node);
    free(d);

    (void) core;
}

static void VS_CC separateRowsCreate(const VSMap* in, VSMap* out, void* userData,
    VSCore* core, const VSAPI* vsapi) {
    SeparateRowsData d;
    SeparateRowsData* data;

    d.node = vsapi->mapGetNode(in, "clip", 0, 0);
    d.interval = (int)vsapi->mapGetInt(in, "interval", 0, 0);

    const VSVideoInfo* vi = vsapi->getVideoInfo(d.node);

    if (!vsh::isConstantVideoFormat(vi)) {
        vsapi->mapSetError(out, "SeparateRows: only constant format input supported");
        vsapi->freeNode(d.node);
        return;
    }

    if (d.interval <= 0) {
        vsapi->mapSetError(out, "SeparateRows: interval must be positive");
        vsapi->freeNode(d.node);
        return;
    }

    for (int plane = 0; plane < vi->format.numPlanes; plane++) {
        int plane_height = vi->height >> (plane ? vi->format.subSamplingH : 0);
        if (plane_height % d.interval != 0) {
            vsapi->mapSetError(out, "SeparateRows: height (including subsampled planes) must be divisible by interval");
            vsapi->freeNode(d.node);
            return;
        }
    }

    data = (SeparateRowsData*)malloc(sizeof(d));
    *data = d;

    VSVideoInfo vi_out = *vi;
    vi_out.height /= d.interval;
    vi_out.numFrames *= d.interval;

    VSFilterDependency deps[] = { {d.node, rpGeneral} };
    vsapi->createVideoFilter(out, "SeparateRows", &vi_out, separateRowsGetFrame,
        separateRowsFree, fmParallel, deps, 1, data, core);

    (void) userData;
}

static void VS_CC weaveRowsCreate(const VSMap* in, VSMap* out, void* userData,
    VSCore* core, const VSAPI* vsapi) {
    WeaveRowsData d;
    WeaveRowsData* data;

    d.node = vsapi->mapGetNode(in, "clip", 0, 0);
    d.period = (int)vsapi->mapGetInt(in, "period", 0, 0);

    const VSVideoInfo* vi = vsapi->getVideoInfo(d.node);

    if (!vsh::isConstantVideoFormat(vi)) {
        vsapi->mapSetError(out, "WeaveRows: only constant format input supported");
        vsapi->freeNode(d.node);
        return;
    }

    if (d.period <= 0) {
        vsapi->mapSetError(out, "WeaveRows: period must be positive");
        vsapi->freeNode(d.node);
        return;
    }

    data = (WeaveRowsData*)malloc(sizeof(d));
    *data = d;

    VSVideoInfo vi_out = *vi;
    vi_out.height *= d.period;
    vi_out.numFrames = (vi->numFrames + d.period - 1) / d.period;

    VSFilterDependency deps[] = { {d.node, rpGeneral} };
    vsapi->createVideoFilter(out, "WeaveRows", &vi_out, weaveRowsGetFrame,
        weaveRowsFree, fmParallel, deps, 1, data, core);

    (void) userData;
}

VS_EXTERNAL_API(void) VapourSynthPluginInit2(VSPlugin* plugin, const VSPLUGINAPI* vspapi) {
    vspapi->configPlugin("com.yuygfgg.rowmanip", "rowmanip", "Vapoursynth SeparateRows/WeaveRows Plugin",
        VS_MAKE_VERSION(1, 0), VAPOURSYNTH_API_VERSION, 0, plugin);
    vspapi->registerFunction("SeparateRows", "clip:vnode;interval:int;", "clip:vnode;",
        separateRowsCreate, nullptr, plugin);
    vspapi->registerFunction("WeaveRows", "clip:vnode;period:int;", "clip:vnode;",
        weaveRowsCreate, nullptr, plugin);
}