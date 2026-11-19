#define DLIB_LOG_DOMAIN "BezierPath"
#include <dmsdk/sdk.h>
#include <dmsdk/dlib/jobsystem.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include "raster_job.h"
#include "fill.h"
#include "shadow.h"
#include "utils.h"

namespace {
using Clock = std::chrono::steady_clock;

struct RasterJob {
    uint32_t id;
    HJob job;
    BufferInfo buffer;
    dmBuffer::HBuffer destination;
    PathPointList path;
    PathPointList mask;
    std::vector<uint8_t> pixels;
    RasterOperation operation;
    RasterParameters params;
    bool ready;
    bool cancelled;
    bool timing;
    Clock::time_point submitted;
    double queue_seconds;
    double work_seconds;
};

HJobContext context;
RasterJob* pending;
uint32_t next_id;
bool measure_jobs;

bool is_large(const BufferInfo& buffer, PathPointList* path, RasterOperation operation, const RasterParameters& params)
{
    const bool shadow = operation == RASTER_SHADOW || operation == RASTER_MASKED_SHADOW;
    // Blurring the coverage adds work across the shadow bounds. Let the
    // worker pick up medium-sized board shadows during cooperative loads.
    const float threshold = shadow ? 320.0f * 320.0f : 512.0f * 512.0f;
    if (float(buffer.width) * buffer.height < threshold)
        return false;
    if (operation == RASTER_MASK && params.inner_or_inverted)
        return true;

    const float padding = shadow ? std::max(1, params.radius) : 1;
    float x0, y0, x1, y1;
    if (!get_path_bounds(path, padding, x0, y0, x1, y1))
        return false;
    const float width = std::min(x1, buffer.origin_x + buffer.width) - std::max(x0, buffer.origin_x);
    const float height = std::min(y1, buffer.origin_y + buffer.height) - std::max(y0, buffer.origin_y);
    return width > 0 && height > 0 && std::ceil(width) * std::ceil(height) >= threshold;
}

int32_t process(HJobContext, HJob, void*, void* data)
{
    RasterJob& work = *static_cast<RasterJob*>(data);
    Clock::time_point started;
    if (work.timing) started = Clock::now();
    RasterScratch scratch;
    const RasterParameters& p = work.params;
    const int* c = p.color;
    const int* end = p.end_color;
    switch (work.operation) {
        case RASTER_FILL:
            fill_path(work.buffer, &work.path, scratch, c[0], c[1], c[2], c[3]);
            break;
        case RASTER_GRADIENT:
            fill_path_gradient(work.buffer, &work.path, scratch, c[0], c[1], c[2], c[3],
                               end[0], end[1], end[2], end[3], p.angle);
            break;
        case RASTER_SHADOW:
            fill_shadow(work.buffer, &work.path, scratch, p.offset_x, p.offset_y, p.radius,
                        c[0], c[1], c[2], c[3], p.inner_or_inverted);
            break;
        case RASTER_MASK:
            mask_path(work.buffer, &work.path, scratch, p.inner_or_inverted);
            break;
        case RASTER_MASKED_SHADOW:
            fill_shadow(work.buffer, &work.path, scratch, p.offset_x, p.offset_y, p.radius,
                        c[0], c[1], c[2], c[3], false);
            mask_path(work.buffer, p.mask_handle ? &work.mask : &work.path, scratch, false);
            break;
    }
    if (work.timing) {
        work.work_seconds = std::chrono::duration<double>(Clock::now() - started).count();
        work.queue_seconds = std::chrono::duration<double>(started - work.submitted).count();
    }
    return 0;
}

void retire()
{
    delete pending;
    pending = 0;
}

void cancel_pending()
{
    pending->cancelled = true;
    if (JobSystemCancelJob(context, pending->job) == JOBSYSTEM_RESULT_INVALID_HANDLE)
        retire();
}

void complete(HJobContext, HJob, JobSystemStatus status, void*, void* data, int32_t)
{
    RasterJob* work = static_cast<RasterJob*>(data);
    if (work->cancelled || status != JOBSYSTEM_STATUS_FINISHED)
        retire();
    else
        work->ready = true;
}
}

void initialize_raster_jobs()
{
#if !defined(__EMSCRIPTEN__) || defined(__EMSCRIPTEN_PTHREADS__)
    JobSystemCreateParams params = {};
    params.m_ThreadNamePrefix = "bezier";
    params.m_ThreadCount = 1;
    context = JobSystemCreate(&params);
#endif
}

void update_raster_jobs()
{
    if (context)
        JobSystemUpdate(context, 0);
}

void finalize_raster_jobs()
{
    if (!context)
        return;
    if (pending)
        pending->cancelled = true;
    JobSystemDestroy(context);
    context = 0;
    retire();
}

int begin_raster_job(lua_State* L, const BufferInfo& buffer, RasterOperation operation, const RasterParameters& params)
{
    if (!context || JobSystemGetWorkerCount(context) == 0)
        return 0;
    PathPointList* source = get_path_points(buffer.path_handle);
    if (!source || !is_large(buffer, source, operation, params))
        return 0;
    if (pending) {
        lua_pushnil(L);
        lua_pushliteral(L, "busy");
        return 2;
    }

    PathPointList* mask = params.mask_handle ? get_path_points(params.mask_handle) : 0;

    RasterJob* work = new RasterJob();
    if (++next_id == 0)
        ++next_id;
    work->id = next_id;
    work->operation = operation;
    work->params = params;
    work->path.points = source->points;
    work->path.is_closed = source->is_closed;
    if (mask) {
        work->mask.points = mask->points;
        work->mask.is_closed = mask->is_closed;
    }
    work->buffer = buffer;
    work->destination = buffer.buffer;
    work->buffer.buffer = 0;
    work->buffer.path_handle = 0;
    work->pixels.assign(buffer.bytes, buffer.bytes + buffer.src_size);
    work->buffer.bytes = work->pixels.data();

    Job job = {};
    job.m_Process = process;
    job.m_Callback = complete;
    job.m_Data = work;
    work->job = JobSystemCreateJob(context, &job);
    if (!work->job) {
        delete work;
        return luaL_error(L, "Unable to allocate raster job");
    }
    pending = work;
    work->timing = measure_jobs;
    if (work->timing) work->submitted = Clock::now();
    if (JobSystemPushJob(context, work->job) != JOBSYSTEM_RESULT_OK) {
        cancel_pending();
        return luaL_error(L, "Unable to submit raster job");
    }
    lua_pushnumber(L, work->id);
    return 1;
}

int poll_raster_job(lua_State* L)
{
    const uint32_t id = (uint32_t)luaL_checknumber(L, 1);
    if (!pending || pending->id != id || pending->cancelled)
        return luaL_error(L, "Raster job is unavailable");
    if (!pending->ready)
        JobSystemUpdate(context, 0);
    if (!pending)
        return luaL_error(L, "Raster job is unavailable");
    if (!pending->ready)
        return 0;
    const BufferInfo buffer = read_and_validate_buffer_info(L, 2);
    if (buffer.buffer != pending->destination || buffer.src_size != pending->pixels.size()) {
        retire();
        return luaL_error(L, "Raster job destination changed");
    }
    std::memcpy(buffer.bytes, pending->pixels.data(), buffer.src_size);
    lua_pushboolean(L, 1);
    const int results = pending->timing ? 3 : 1;
    if (pending->timing) {
        lua_pushnumber(L, pending->queue_seconds);
        lua_pushnumber(L, pending->work_seconds);
    }
    retire();
    return results;
}

int set_raster_job_timing(lua_State* L)
{
    measure_jobs = lua_toboolean(L, 1);
    return 0;
}

int cancel_raster_job(lua_State* L)
{
    const uint32_t id = (uint32_t)luaL_checknumber(L, 1);
    if (!pending || pending->id != id || pending->cancelled)
        return 0;
    if (pending->ready) {
        retire();
    } else {
        cancel_pending();
    }
    return 0;
}
