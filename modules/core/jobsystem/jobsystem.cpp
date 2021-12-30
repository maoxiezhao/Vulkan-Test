#include "jobsystem.h"
#include "core\memory\memory.h"
#include "core\platform\fiber.h"
#include "core\platform\platform.h"
#include "core\platform\sync.h"
#include "core\platform\atomic.h"

#pragma warning( push )
#pragma warning (disable : 6385)

namespace VulkanTest
{
namespace Jobsystem
{
    //////////////////////////////////////////////////////////////
    // Definition
    const U32 MAX_FIBER_COUNT = 512;
    const U32 MAX_JOB_HANDLE_COUNT = 4096;
    const U32 HANDLE_ID_MASK = 0x0000ffff;
    const U32 HANDLE_GENERATION_MASK = 0xffff0000;

#ifdef _WIN32
    static void __stdcall FiberFunc(void* data);
#else
    static void FiberFunc(void* data);
#endif

    struct ManagerImpl;
    struct WorkerThread;

    struct JobImpl
    {
        void (*task)(void*) = nullptr;
        void* data = nullptr;
        JobHandle finishHandle;
        JobHandle precondition;
        U8 workerIndex;
    };

    struct JobCounter
    {
        volatile I32 value = 0;
        JobImpl nextJob;
        JobHandle sibling = INVALID_HANDLE;
        U32 generation = 0;	// use for check
    };

    struct WorkerFiber
    {
        U32 index = 0;
        Fiber::Handle handle = Fiber::INVALID_HANDLE;
        JobImpl currentJob;
    };

    static thread_local WorkerThread* gWorker = nullptr;
    static LocalPtr<ManagerImpl> gManager;

    WorkerThread* GetWorker()
    {
        return gWorker;
    }

    struct WorkerThread : Thread
    {
    public:
        U32 workderIndex;
        ManagerImpl& manager;
        bool isFinished = false;
        Fiber::Handle primaryFiber = Fiber::INVALID_HANDLE;
        WorkerFiber* currentFiber = nullptr;

    public:
        WorkerThread(ManagerImpl& manager_, U32 workerIndex_) :
            manager(manager_),
            workderIndex(workerIndex_)
        {
        }

        ~WorkerThread()
        {
        }

        int Task() override
        {
            gWorker = this;
            primaryFiber = Fiber::Create(Fiber::THIS_THREAD);
            gManager->sync.Lock();

            WorkerFiber* fiber = gManager->freeFibers.back();
            gManager->freeFibers.pop_back();
            if (!Fiber::IsValid(fiber->handle))
                fiber->handle = Fiber::Create(64 * 1024, FiberFunc, fiber);

            gWorker->currentFiber = fiber;
            Fiber::SwitchTo(gWorker->primaryFiber, fiber);
            return 0;
        }
    };

    struct ManagerImpl
    {
    public:
        Mutex sync;
        ConditionMutex jobQueueLock;
        JobCounter handleCounters[MAX_JOB_HANDLE_COUNT];
        std::vector<U32> handlePool;
        std::vector<WorkerFiber*> readyFibers;
        std::vector<WorkerFiber*> freeFibers;
        WorkerFiber fiberPool[MAX_FIBER_COUNT];
        std::vector<WorkerThread*> workers;
        std::vector<JobImpl> jobQueue;

    public:
        ManagerImpl()
        {
            handlePool.resize(MAX_JOB_HANDLE_COUNT);
            for (int i = 0; i < MAX_JOB_HANDLE_COUNT; i++)
            {
                handleCounters[i].sibling = INVALID_HANDLE;
                handlePool[i] = i;;
            }
        }

        JobHandle AllocateHandle()
        {
            if (handlePool.empty())
                return INVALID_HANDLE;

            U32 handle = handlePool.back();
            handlePool.pop_back();
            JobCounter& counter = handleCounters[handle & HANDLE_ID_MASK];
            counter.value = 1;
            counter.sibling = INVALID_HANDLE;
            counter.nextJob.task = nullptr;
          
            return (handle & HANDLE_ID_MASK) | counter.generation;
        }

        void IncHandle(JobHandle* jobHandle)
        {
            ASSERT(jobHandle != nullptr);
            ScopedMutex lock(sync);
            if (IsHandleValid(*jobHandle) && !IsHandleZero(*jobHandle, false))
                handleCounters[*jobHandle & HANDLE_ID_MASK].value++;
            else
                *jobHandle = AllocateHandle();
        }

        void DecHandle(JobHandle* jobHandle)
        {
            ASSERT(jobHandle != nullptr);
            ScopedMutex lock(sync);
            I32 value = AtomicDecrement(&handleCounters[*jobHandle & HANDLE_ID_MASK].value);
            if (value > 0)
                return;
         
            ReleaseHandle(*jobHandle, false);
        }

        bool IsHandleValid(JobHandle jobHandle)const
        {
            return jobHandle != INVALID_HANDLE;
        }

        bool IsHandleZero(JobHandle jobHandle, bool isLock)
        {
            if (!IsHandleValid(jobHandle))
                return false;

            const U32 id = jobHandle & HANDLE_ID_MASK;
            const U32 generation = jobHandle & HANDLE_GENERATION_MASK;
            if (isLock) sync.Lock();
            bool ret = handleCounters[id].value == 0 || handleCounters[id].generation != generation;
            if (isLock) sync.Unlock();
            return true;
        }

        void ReleaseHandle(JobHandle jobHandle, bool isLock)
        {
            if (isLock) sync.Lock();
            JobHandle iter = jobHandle;
            while (IsHandleValid(iter))
            {
                JobCounter& counter = handleCounters[iter & HANDLE_ID_MASK];
                if (counter.nextJob.task != nullptr)
                {
                    jobQueueLock.Enter();
                    PushJob(counter.nextJob);
                    jobQueueLock.Exit();
                }

                counter.generation = ((counter.generation >> 16) & 0xffff + 1) << 16;
                counter.nextJob.task = nullptr;
                handlePool.push_back(iter & HANDLE_ID_MASK | counter.generation);
                iter = counter.sibling;
            }
            if (isLock) sync.Unlock();
        }

        void PushJob(JobImpl job)
        {
            jobQueue.push_back(job);
            for (auto worker : workers)
                worker->Wakeup();
        }
    };

    //////////////////////////////////////////////////////////////
    // Methods

    bool Initialize(U32 numWorkers)
    {
        gManager.Create();

        for (int i = 0; i < MAX_FIBER_COUNT; i++)
        {
            WorkerFiber* fiber = &gManager->fiberPool[i];
            fiber->index = i;
            fiber->handle = Fiber::INVALID_HANDLE;
            gManager->freeFibers.push_back(fiber);
        }

        numWorkers = std::min(64u, numWorkers);
        gManager->workers.reserve(numWorkers);
        for (U32 i = 0; i < numWorkers; i++)
        {
            WorkerThread* worker = CJING_NEW(WorkerThread)(*gManager, (U64)1u << i);
            if (worker->Create("Worker"))
            {
                gManager->workers.push_back(worker);
                worker->SetAffinity((U64)1u << i);
            }
        }

        return true;
    }

    void Uninitialize()
    {
        // Clear workers
        for (auto worker : gManager->workers)
        {
            worker->isFinished = true;
            worker->Wakeup();
        }
        for (auto worker : gManager->workers)
        {
            while (!worker->IsFinished())
                worker->Wakeup();
            worker->Destroy();
            CJING_SAFE_DELETE(worker);
        }
        gManager->workers.clear();

        // Clear fibers
        for (auto& fiber : gManager->fiberPool)
        {
            if (Fiber::IsValid(fiber.handle))
                Fiber::Destroy(fiber.handle);
        }

        gManager.Destroy();
    }

    void Run(const JobInfo& jobInfo, JobHandle* handle)
    {

    }

    void Run(void*data, JobFunc func, JobHandle* handle)
    {

    }

    void RunEx(void* data, JobFunc func, JobHandle* handle, JobHandle precondition)
    {

    }

    void Wait(JobHandle handle)
    {

    }

#ifdef _WIN32
    static void __stdcall FiberFunc(void* data)
#else
    static void FiberFunc(void* data);
#endif
    {
        gManager->sync.Unlock();

        WorkerFiber* currentFiber = (WorkerFiber*)(data);
        WorkerThread* worker = GetWorker();
        while (!worker->isFinished)
        {
            JobImpl job;
            while (!worker->isFinished)
            {
                gManager->jobQueueLock.Enter();
                if (!gManager->jobQueue.empty())
                {
                    job = gManager->jobQueue.back();
                    gManager->jobQueue.pop_back();
                    break;
                }

                worker->Sleep(gManager->jobQueueLock);
                gManager->jobQueueLock.Exit();
            }

            if (worker->isFinished)
                break;

            if (job.task != nullptr)
            {
                currentFiber->currentJob = job;
                job.task(job.data);
                currentFiber->currentJob.task = nullptr;

                if (gManager->IsHandleValid(job.finishHandle))
                    gManager->DecHandle(&job.finishHandle);

                worker = GetWorker();
            }
        }

        Fiber::SwitchTo(currentFiber->handle, worker->primaryFiber);
    }
}
}

#pragma warning (pop)