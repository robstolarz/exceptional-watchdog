// TypeScript declarations for exceptional-watchdog

declare module "exceptional-watchdog" {
  /**
   * Feed the watchdog to reset and start the timer with a specified duration in milliseconds.
   * @param millis The number of milliseconds to set the watchdog timer to.
   */
  export function feedDoggo(millis: number): void;
}
