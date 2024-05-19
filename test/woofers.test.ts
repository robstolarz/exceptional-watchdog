import ExceptionalWatchdog from "../src";
import { statSync } from "fs";

function blockingOperation(timeout: number): void {
  // Record the start time
  const startTime = Date.now();

  // Spin in a loop until `timeout` seconds have passed or an exception is thrown
  while (true) {
    // intentionally deoptimize the loop so we have a chance of stopping it
    statSync(".");
    if (Date.now() - startTime > timeout) {
      break;
    }
  }
}

describe("exceptional-watchdog", () => {
  it("barks when not fed", async () => {
    ExceptionalWatchdog.feedDoggo(1000);

    let threw = false;
    try {
      blockingOperation(5000);
    } catch (e) {
      threw = true;
    }

    expect(threw).toBe(true);
  });
});
