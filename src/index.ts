import bindings from "bindings";
const ExceptionalWatchdog = bindings("exceptional-watchdog");

export function feedDoggo(millis: number): void {
  ExceptionalWatchdog.feedDoggo(millis);
}
