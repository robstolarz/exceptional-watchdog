# exceptional-watchdog

## What

Have you ever had a loop in your Node app get stuck? Ever seen your requests start to fail because some chunk of code was taking just a little too long?

Meet the goodest boy that there ever was: `exceptional-watchdog`. This 'lil guy will make sure you feed him regularly, or he'll bark so hard your app throws an exception! In most cases, this can help your app shake itself loose from an unexpected synchronous stall (with a few [prominent caveats](#Caveats) that you need to read before using this).

## Usage

For now, please don't use `exceptional-watchdog` inside a library. Keep it at the root of your application, because there can only be one global dog per isolate (for the moment; see [v2](#v2)).

You'll want to do two things:

1. Feed the watchdog regularly, so it doesn't bark unless the app is truly stalled out
2. Feed the watchdog right before a new batch of work starts, so that batch of work has exactly the expected amount of time to run

Start by importing the module and deciding your pup's feeding deadline (irregular feeding is not recommended):

```typescript
import * as ExceptionalWatchdog from 'exceptional-watchdog';
const EWD_FEED_DEADLINE_IN_MS = 10000; // 10 seconds is probably way more than you need
// adventurous users could try 5
```

For #1: Set up a regular feeding cycle. You'll want it to be very frequent, or you may end up being exposed to OS stalls that are just long enough to starve the dog. Every 1/10th-second is a good place to start:

```typescript
setInterval(() => ExceptionalWatchdog.feedDoggo(EWD_FEED_DEADLINE_IN_MS), 100);
```

This might seem a little counterintuitive, as it looks like it's unconditionally feeding the watchdog, but rest assured that this code will not run if the JS thread has stalled out somewhere; `setInterval` doesn't have the power to interrupt synchronous code (and in fact not much can; see the [caveats](#Caveats) section).

For #2: Add an additional call right before the "job to be done" begins. In the example of an Express-based web application, you'll at the least want to feed the watchdog at the start of every request, possibly using a middleware:

```typescript
import express, { Request, Response, NextFunction } from 'express';
const app = express();

// should probably be the first middleware
app.use((_req: Request, _res: Response, next: NextFunction) => {
  ExceptionalWatchdog.feedDoggo(EWD_FEED_DEADLINE_IN_MS);
  next();
});
```

Note that this is _not_ a way to provide your request with a timeout, as a request can stay alive for a very long time while not blocking the event loop. Remember, this doesn't prevent requests from going on too long, it prevents synchronous stalls. So in the case that you expect to do more work, you can feed the watchdog right before that work for additional predictability.

It is better to have too few calls than too many, though, and it's definitely a terrible idea to wrap this in layers of functions or obscure the control flow in any way. When in doubt, leave all calls to the watchdog in the top level of your app.

## How?

Briefly put:

1. We want to know when the app is stalled
2. We want to deliver an exception to the app when it stalls

To make the first happen, we want a timer that isn't stuck when JS is stuck, and we expect that timer to be continually reset by the JS thread before it expires. Conveniently, libuv is perfectly accessible to us, so we just kick off a secondary event loop on another thread and stick a timer on it while making it accessible to JS.

For the second, we (ab)use an interesting pair of methods on the `v8::Isolate` that imports our module. Isolates expose a `ThrowError` method that lets you just lob an error right into the middle of execution (as well as similar `Throw*` methods that have a little more specificity as to what's thrown). You can't just call this _whenever_, as doing basically anything to a V8 isolate as it's running is a big no-no that will probably crash your app. However, you can politely ask your V8 isolate to take a break so you can ask it to do something, using the `RequestInterrupt` method. With this, the V8 isolate is in a good spot to hear our pupper bark, and bark it does.

## Caveats

### Native code is uninterruptible / V8 is sometimes too optimized

Sometimes an app gets stuck in a particularly large `JSON.parse`/`JSON.stringify` call. `exceptional-watchdog` can't help with this case. Really, nothing can. When V8 is running "native code", it's doing stuff that isn't easily recovered from. V8 is optimized drastically towards speed. In some native code (like `JSON.*` methods) it actually turns off the GC and manipulates memory directly. There's no way to interrupt work like this safely in a generalizable way.

Additionally, JS code can **become** native. If you have some code that is hot-and-stable enough, the just-in-time compiler will make it faster by compiling it to native machine code that your processor runs directly. This code runs with the same lack of affordances that native V8 engine implementations of JS functions would, except sometimes the JIT will emit interrupt checks. It's not guaranteed to (so something like a `while (true) { /* ... */ }` is actually truly uninterruptible sometimes) but it can in some cases. In those cases, `exceptional-watchdog` will still function roughly as expected. You can determine what all is happening using the `--trace-*` options to Node, and the test suites can be a fun playground for seeing what is "too optimal" to be interrupted.

In these cases, although the work can't be interrupted, the bad code will still be punished if we ever escape it. For example, it may look like a loop construct itself has thrown an error (because the interrupt check happened on loop exit), and that may happen entire seconds after the deadline time expected.

**TL;DR: `exceptional-watchdog` is not guaranteed to cause recovery from stalls caused by synchronous code; it is only guaranteed to eventually attempt to punish stalling if that code eventually becomes interruptible.**

This won't be fixed or mitigated. Hopefully issues are punished and identified on a smaller scale before they cause cataclysmic stalls.

### Watchdog's tolerance of system time changes is questionable

I (Rob) have no idea what time the libuv timers use. It is possible that a cloud provider may pause the system on which the code runs and then later unpause it; I have no idea if that will cause the libuv timers to fire because they've jumped forward in time.

`exceptional-watchdog` currently expects a very traditional computing environment and for an `uncaughtException` hook to have been registered with Node so unexpected exceptions don't tear down the application.

I am unlikely to fix, mitigate, or even test this as it is not that interesting to me; not least of all because unusual computing environments tend to have timeouts for executions built in. That being said, patches are welcome.

### Exceptions are too easily ignored

If the watchdog barks in the middle of a try/catch block that doesn't rethrow, the exception will just pass silently. In case there was ever any doubt, it's best to keep the use of exceptions for exceptional purposes and not as part of expected usage of an API.

Given that there exists code out there that ignores this rule of thumb, it may be possible to mitigate this issue by throwing an exception and immediately requesting another interrupt multiple times, and increasing the depth of this stack each time it's attempted until the watchdog is eventually fed again. This will probably also cause considerable noise in whatever error reporting tools are in use. Another possible option is to use V8 internal methods to escape the `catch` block, which makes this package much more complex and prone to becoming buggy and more difficult to maintain.

This might be something worth exploring for a v2 or v3. No promises, ideas welcome.

### Exceptions may be delivered in unexpected places

It is straightforwardly possible to cause a condition where some work has just completed but the next work to be done has not fed the watchdog. Consider the case where:

* a request A had a very long synchronous task that blocked the watchdog from being fed, but finished just before the deadline
* a request B is just about to start being processed
* the watchdog deadline expires before the middleware that would feed the watchdog is triggered

The exception may be delivered during the processing of request B or even in library code before the processing of request B begins, which would lead to confusion.

Fixing this is actually not that difficult with the appropriate use of [`AsyncLocalStorage`](https://nodejs.org/api/async_context.html). It would be fairly straightforward to implement a check during the interruption that identifies whether or not the code in question belongs to an Async context for which a watchdog has been instantiated. This would allow for the use of an arbitrary number of watchdogs with a single secondary event loop. Watchdogs would then have finalizers attached to their async context objects, which would cause them to be torn down once inaccessible; a permissive adoption mode would allow these watchdogs to pass on even if in a barking state, while a strict adoption mode would require that they be fed a final meal or otherwise cause barking at disposal time. It may even be reasonable to give them a bounded lifetime, which causes barking if they live for too long regardless of feeding state.

This is great fodder for a v2 and should eventually be implemented. The global watchdog API could remain in v2 but be implemented atop this new approach. It would probably be best to remove the global watchdog API in a v3.

### Watchdog gives up after one try

Maybe it should try a few times? Maybe it shouldn't stop trying until fed? Maybe it should be configurable? Unclear but probably worth thinking about for a v2.

### Watchdog cannot currently be stopped

It would be reasonable to be able to stop the watchdog, at least for testing. Probably should happen for a v1.

## Using in production

I'm not going to stop you. I don't know if I'd recommend it, this is more or less one afternoon's worth of work, but it does generally do what you'd expect.

In `exceptional-watchdog`'s defense, it doesn't touch any V8 internals. It uses only public V8 APIs and respectfully performs each operation. It has a test. It is packaged to be usable in a pinch. So there's that.

To be clear: I (Rob) wrote this because I thought it'd be funny for something like this to exist, and because I wondered why it didn't already. If you want to use this for anything other than satisfying your intellectual curiosity, that's fine, but please don't expect support or to hold me liable. This section is not a list of terms you are agreeing to when using this code; please review the LICENSE file included for a complete accounting of rights granted to you and the conditions under which you retain them.

That being said, if you do end up using this in production, please file a GitHub Issue saying hello (no need to include your company name, though that would be especially neat).

## Roadmap

### v1

- [ ] Make teardown possible
- [ ] Attempt repeated delivery of exceptions when left unacknowledged (on timer)
- [ ] Set up CI, linting, packaging
- [ ] Make debug logging configurable or at least not so noisy

### v2

- [ ] AsyncLocalStorage stuff

... more?
