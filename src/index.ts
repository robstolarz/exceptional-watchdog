/// <reference path="./index.d.ts" />
// the above makes the ambient type decl usable here
// TODO: determine if this is actually the right way to do it
import bindings from "bindings";
import type ExceptionalWatchdogModuleType from "exceptional-watchdog";
const ExceptionalWatchdog = bindings(
  "exceptional-watchdog",
) as typeof ExceptionalWatchdogModuleType;

export default ExceptionalWatchdog;
