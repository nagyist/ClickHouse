#pragma once

#include <Core/Block_fwd.h>
#include <QueryPipeline/QueryPlanResourceHolder.h>
#include <QueryPipeline/SizeLimits.h>
#include <QueryPipeline/StreamLocalLimits.h>

#include <functional>

namespace DB
{

class InputPort;
class OutputPort;

class IProcessor;
using ProcessorPtr = std::shared_ptr<IProcessor>;
using Processors = std::vector<ProcessorPtr>;

class QueryStatus;
using QueryStatusPtr = std::shared_ptr<QueryStatus>;

struct Progress;
using ProgressCallback = std::function<void(const Progress & progress)>;

struct StreamLocalLimits;
class EnabledQuota;

class Block;
class Pipe;
class Chain;
class IOutputFormat;
class SinkToStorage;
class ISource;
class ISink;
class ReadProgressCallback;

struct ColumnWithTypeAndName;
using ColumnsWithTypeAndName = std::vector<ColumnWithTypeAndName>;

class QueryResultCacheWriter;

class SourceFromChunks;

class QueryPipeline
{
public:
    QueryPipeline();
    QueryPipeline(QueryPipeline &&) noexcept;
    QueryPipeline(const QueryPipeline &) = delete;

    QueryPipeline & operator=(QueryPipeline &&) noexcept;
    QueryPipeline & operator=(const QueryPipeline &) = delete;

    ~QueryPipeline();

    /// pulling or completed
    explicit QueryPipeline(Pipe pipe);
    /// pulling
    explicit QueryPipeline(std::shared_ptr<ISource> source);
    /// pushing
    explicit QueryPipeline(Chain chain);
    explicit QueryPipeline(std::shared_ptr<SinkToStorage> sink);
    explicit QueryPipeline(std::shared_ptr<IOutputFormat> format);

    /// completed
    QueryPipeline(
        QueryPlanResourceHolder resources_,
        std::shared_ptr<Processors> processors_);

    /// pushing
    QueryPipeline(
        QueryPlanResourceHolder resources_,
        std::shared_ptr<Processors> processors_,
        InputPort * input_);

    /// pulling
    QueryPipeline(
        QueryPlanResourceHolder resources_,
        std::shared_ptr<Processors> processors_,
        OutputPort * output_,
        OutputPort * totals_ = nullptr,
        OutputPort * extremes_ = nullptr);

    bool initialized() const { return !processors->empty(); }
    /// When initialized, exactly one of the following is true.
    /// Use PullingPipelineExecutor or PullingAsyncPipelineExecutor.
    bool pulling() const { return output != nullptr; }
    /// Use PushingPipelineExecutor or PushingAsyncPipelineExecutor.
    bool pushing() const { return input != nullptr; }
    /// Use PipelineExecutor. Call execute() to build one.
    bool completed() const { return initialized() && !pulling() && !pushing(); }

    /// Only for pushing.
    void complete(Pipe pipe);
    /// Only for pulling.
    void complete(std::shared_ptr<IOutputFormat> format);
    void complete(Chain chain);
    void complete(std::shared_ptr<SinkToStorage> sink);
    void complete(std::shared_ptr<ISink> sink);

    /// Only for pushing and pulling.
    Block getHeader() const;
    SharedHeader getSharedHeader() const;

    size_t getNumThreads() const { return num_threads; }
    void setNumThreads(size_t num_threads_) { num_threads = num_threads_; }

    bool getConcurrencyControl() const { return concurrency_control; }
    void setConcurrencyControl(bool concurrency_control_) { concurrency_control = concurrency_control_; }

    void setProcessListElement(QueryStatusPtr elem);
    void setProgressCallback(const ProgressCallback & callback);
    void setLimitsAndQuota(const StreamLocalLimits & limits, std::shared_ptr<const EnabledQuota> quota_);
    bool tryGetResultRowsAndBytes(UInt64 & result_rows, UInt64 & result_bytes) const;

    void writeResultIntoQueryResultCache(std::shared_ptr<QueryResultCacheWriter> query_result_cache_writer);
    void finalizeWriteInQueryResultCache();
    void readFromQueryResultCache(
        std::unique_ptr<SourceFromChunks> source,
        std::unique_ptr<SourceFromChunks> source_totals,
        std::unique_ptr<SourceFromChunks> source_extremes);

    void setQuota(std::shared_ptr<const EnabledQuota> quota_);

    void addStorageHolder(StoragePtr storage);

    /// Existing resources are not released here, see move ctor for QueryPlanResourceHolder.
    void addResources(QueryPlanResourceHolder holder) { resources = std::move(holder); }

    /// Skip updating profile events.
    /// For merges in mutations it may need special logic, it's done inside ProgressCallback.
    void disableProfileEventUpdate() { update_profile_events = false; }

    /// Create progress callback from limits and quotas.
    std::unique_ptr<ReadProgressCallback> getReadProgressCallback() const;

    /// Add processors and resources from other pipeline. Other pipeline should be completed.
    void addCompletedPipeline(QueryPipeline other);

    const Processors & getProcessors() const { return *processors; }

    /// For pulling pipeline, convert structure to expected.
    /// Trash, need to remove later.
    void convertStructureTo(const ColumnsWithTypeAndName & columns);

    void reset();
    void cancel() noexcept;

private:
    QueryPlanResourceHolder resources;

    ProgressCallback progress_callback;
    std::shared_ptr<const EnabledQuota> quota;
    bool update_profile_events = true;

    std::shared_ptr<Processors> processors;

    InputPort * input = nullptr;

    OutputPort * output = nullptr;
    OutputPort * totals = nullptr;
    OutputPort * extremes = nullptr;

    QueryStatusPtr process_list_element;

    IOutputFormat * output_format = nullptr;

    size_t num_threads = 0;
    bool concurrency_control = false;

    friend class PushingPipelineExecutor;
    friend class PullingPipelineExecutor;
    friend class PushingAsyncPipelineExecutor;
    friend class PullingAsyncPipelineExecutor;
    friend class CompletedPipelineExecutor;
    friend class RefreshTask;
    friend class QueryPipelineBuilder;
};

}
