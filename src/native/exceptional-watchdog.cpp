#include <napi.h>
#include <node.h>
#include <uv.h>
#include <v8.h>
#include <thread>
#include <iostream>

static uv_loop_t* secondary_loop;
static uv_timer_t* timer_req;
static v8::Isolate* main_isolate = nullptr;

// This will be called when the interrupt is requested
static void interrupt_callback(v8::Isolate* isolate, void* data) {
    v8::HandleScope handle_scope(main_isolate);

    std::cout << "locking" << std::endl;
    v8::Locker locker(main_isolate);

    std::cout << "throwing" << std::endl;
    main_isolate->ThrowError("puppy hungwy 🚨🐶");
}

// Callback for when the timer expires
void timer_expired(uv_timer_t* handle) {
    // Request an interrupt on the main V8 isolate
    std::cout << "interrupting" << std::endl;
    main_isolate->RequestInterrupt(interrupt_callback, nullptr);
    std::cout << "stopping timer (timer_expired)" << std::endl;
    uv_timer_stop(handle);
}

// Function to reset the watchdog timer
static void feedDoggo(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    if (info.Length() < 1 || !info[0].IsNumber()) {
        Napi::TypeError::New(env, "Please specify the feeding time in milliseconds 🕒🍖").ThrowAsJavaScriptException();
        return;
    }

    int millis = info[0].As<Napi::Number>().Int32Value();

    // Either start or reset the timer
    std::cout << "stopping timer (feedDoggo)" << std::endl;
    uv_timer_stop(timer_req);
    std::cout << "starting timer (feedDoggo)" << std::endl;
    uv_timer_start(timer_req, timer_expired, millis, 0); // Set for specified milliseconds

    return;
}

void idle_cb(uv_idle_t* handle) {
    // This callback does basically nothing, effectively keeping the loop alive
}

// Initialize secondary loop but do not start the timer
void init_secondary_loop() {
    std::cout << "initing loop" << std::endl;
    secondary_loop = new uv_loop_t();
    uv_loop_init(secondary_loop);

    std::cout << "initing timer" << std::endl;
    timer_req = new uv_timer_t();
    uv_timer_init(secondary_loop, timer_req);

    std::cout << "initing idle handle" << std::endl;
    uv_idle_t* idle_req;
    idle_req = new uv_idle_t();
    uv_idle_init(secondary_loop, idle_req);
    uv_idle_start(idle_req, idle_cb);

    std::thread loop_thread([]() {
        std::cout << "starting secondary loop" << std::endl;
        // this runs until it is stopped
        // FIXME: use a finalizer elsewhere to actually stop this loop, because today it doesn't
        // TODO: add an API function to stop this loop, at least for test purposes
        uv_run(secondary_loop, UV_RUN_DEFAULT);
        std::cout << "closing secondary loop" << std::endl;
        uv_loop_close(secondary_loop);
        std::cout << "deleting secondary loop" << std::endl;
        delete secondary_loop;
    });
    std::cout << "detaching secondary loop thread" << std::endl;
    loop_thread.detach();
}

// This will automatically initialize the watchdog timer upon module load
Napi::Object Init(Napi::Env env, Napi::Object exports) {
    main_isolate = v8::Isolate::GetCurrent(); // Capture the main isolate
    init_secondary_loop();  // Set up the secondary event loop
    exports.Set(Napi::String::New(env, "feedDoggo"), Napi::Function::New(env, &feedDoggo));
    return exports;
}

NODE_API_MODULE(exceptional_watchdog, Init)
