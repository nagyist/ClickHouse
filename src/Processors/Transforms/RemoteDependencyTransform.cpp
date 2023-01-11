#include <Processors/Transforms/RemoteDependencyTransform.h>

namespace DB
{

namespace ErrorCodes
{
    extern const int LOGICAL_ERROR;
}

RemoteDependencyTransform::RemoteDependencyTransform(const Block & header)
    : DependentProcessor(InputPorts(1, header), OutputPorts(1, header))
    , data_port(&outputs.front())
{
}

void RemoteDependencyTransform::connectToScheduler(ResizeProcessor & scheduler)
{
    outputs.emplace_back(Block{}, this);
    dependency_port = &outputs.back();
    auto * free_port = scheduler.getFreeInputPortIfAny();
    if (!free_port)
        throw Exception(ErrorCodes::LOGICAL_ERROR, "There are no free input ports in scheduler. This is a bug");

    connect(*dependency_port, *free_port);
}

IProcessor::Status RemoteDependencyTransform::prepare()
{
    Status status = Status::Ready;

    while (status == Status::Ready)
    {
        status = !has_data ? prepareConsume()
                           : prepareGenerate();
    }

    return status;
}

IProcessor::Status RemoteDependencyTransform::prepareConsume()
{
    auto & input = getInputPort();

    /// Check all outputs are finished or ready to get data.

    bool all_finished = true;
    for (auto & output : outputs)
    {
        if (output.isFinished())
            continue;

        all_finished = false;
    }

    if (all_finished)
    {
        input.close();
        return Status::Finished;
    }

    /// Try get chunk from input.
    if (input.isFinished())
    {
        for (auto & output : outputs)
            output.finish();

        return Status::Finished;
    }

    input.setNeeded();
    if (!input.hasData())
        return Status::NeedData;

    chunk = input.pull();
    has_data = true;

    return Status::Ready;
}

IProcessor::Status RemoteDependencyTransform::prepareGenerate()
{
    if (!data_port->isFinished() && !dependency_port->isFinished() && data_port->canPush() && dependency_port->canPush())
    {
        dependency_port->push(Chunk{});
        data_port->push(std::move(chunk));
        has_data = false;
        return Status::Ready;
    }

    if (!dependency_port->isFinished() && dependency_port->canPush())
    {
        dependency_port->push(Chunk{});
        return Status::Ready;
    }

    if (!data_port->isFinished() && data_port->canPush())
    {
        data_port->push(std::move(chunk));
        has_data = false;
        return Status::Ready;
    }

    return Status::PortFull;
}


ReadFromMergeTreeDependencyTransform::ReadFromMergeTreeDependencyTransform(const Block & header)
    : DependentProcessor(InputPorts(1, header), OutputPorts(1, header))
    , data_port(&inputs.front())
{
}

void ReadFromMergeTreeDependencyTransform::connectToScheduler(ResizeProcessor & scheduler)
{
    inputs.emplace_back(Block{}, this);
    dependency_port = &inputs.back();
    auto * free_port = scheduler.getFreeOutputPortIfAny();
    if (!free_port)
        throw Exception(ErrorCodes::LOGICAL_ERROR, "There are no free input ports in scheduler. This is a bug");

    connect(*free_port, *dependency_port);
}

IProcessor::Status ReadFromMergeTreeDependencyTransform::prepare()
{
    Status status = Status::Ready;

    while (status == Status::Ready)
    {
        status = !has_data ? prepareConsume()
                           : prepareGenerate();
    }

    return status;
}

IProcessor::Status ReadFromMergeTreeDependencyTransform::prepareConsume()
{
    auto & output_port = getOutputPort();

    /// Check all outputs are finished or ready to get data.
    if (output_port.isFinished())
    {
        data_port->close();
        dependency_port->close();
        return Status::Finished;
    }

    /// Try get chunk from input.
    if (data_port->isFinished())
    {
        dependency_port->close();
        output_port.finish();
        return Status::Finished;
    }

    if (!dependency_port->isFinished())
    {
        dependency_port->setNeeded();
        if (!dependency_port->hasData())
            return Status::NeedData;
    }

    data_port->setNeeded();
    if (!data_port->hasData())
        return Status::NeedData;

    if (!dependency_port->isFinished())
        dependency_port->pull();
    chunk = data_port->pull();
    has_data = true;

    return Status::Ready;
}

IProcessor::Status ReadFromMergeTreeDependencyTransform::prepareGenerate()
{
    auto & output_port = getOutputPort();
    bool can_push = !output_port.isFinished() && output_port.canPush();

    if (can_push)
    {
        output_port.push(std::move(chunk));
        has_data = false;
        return Status::Ready;
    }

    return Status::PortFull;
}

}
