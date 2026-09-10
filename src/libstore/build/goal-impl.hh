#pragma once
#include "nix/store/build/goal.hh"
#include "nix/store/build/worker.hh"

namespace nix {

template<typename T>
auto Goal::AwaitableFrameBase::await_transform(AsyncCallback<T> && ac)
{
    struct Awaiter
    {
        fun<void(Callback<T>)> fn;
        std::shared_ptr<std::promise<T>> promise;

        bool await_ready()
        {
            return false;
        }

        void await_suspend(std::coroutine_handle<> handle)
        {
            auto h = HandleTypeBase::from_address(handle.address());
            auto goal = h.promise().goal;
            promise = std::make_shared<std::promise<T>>();

            fn(Callback<T>([promise = promise,
                            maybeWaker = goal->worker.getCrossThreadWaker(),
                            goalWeak = goal->weak_from_this()](std::future<T> res) {
                try {
                    promise->set_value(res.get());
                } catch (...) {
                    promise->set_exception(std::current_exception());
                }

                /* The Worker might have already died (and the waker with it) by
                   the time the callback fired. */
                if (auto waker = maybeWaker.lock())
                    waker->enqueue(goalWeak);
            }));

            goal->worker.waitForCompletion(goal->shared_from_this());
        }

        T await_resume()
        {
            return promise->get_future().get();
        }
    };

    return Awaiter{.fn = std::move(ac.fn)};
}

} // namespace nix
