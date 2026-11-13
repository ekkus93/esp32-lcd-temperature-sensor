// The probe's alert pause for one calibration flow (spec §17.3, review audit F1). One renewal
// schedule runs for the whole open flow, whatever its step; measurement timers are separate. All
// requests go out one after another, so a slow earlier request can never land after the closing
// tail and extend the pause. Closing sends the five-minute tail exactly once.

export interface HoldKeeperOptions {
  // Asks the device to hold the probe's alerts for this many minutes from now.
  send: (minutes: number) => Promise<void>;
  // A renewal failed: the pause is no longer assured. Renewal stops until start() succeeds again.
  onLost: (error: unknown) => void;
  holdMinutes?: number;
  tailMinutes?: number;
  renewMs?: number;
}

export interface HoldKeeper {
  // Starts the pause, or re-establishes it after a loss. True once the device confirmed it and the
  // flow is still open; renewal then runs until close(). False if the flow closed meanwhile. A
  // refusal from the device rejects with its error.
  start(): Promise<boolean>;
  // Ends the flow: stops renewal and sends the tail once, after any request already in flight
  // (nothing, if no pause was ever requested).
  close(): Promise<void>;
  readonly closed: boolean;
  // Whether the pause is currently confirmed (false before start, after a loss, and once closed).
  readonly held: boolean;
}

export function createHoldKeeper({
  send,
  onLost,
  holdMinutes = 15,
  tailMinutes = 5,
  renewMs = 5 * 60 * 1000,
}: HoldKeeperOptions): HoldKeeper {
  let chain: Promise<void> = Promise.resolve();
  let timer: ReturnType<typeof setInterval> | undefined;
  let closed = false;
  let held = false;
  let requested = false; // a pause was asked for, so closing needs a tail
  let tail: Promise<void> | undefined;

  // Queues a request behind every earlier one; the queue survives a failed request.
  function queue(minutes: number): Promise<void> {
    const request = chain.then(() => send(minutes));
    chain = request.catch(() => undefined);
    return request;
  }

  function stopRenewal() {
    if (timer !== undefined) clearInterval(timer);
    timer = undefined;
  }

  async function renew() {
    if (closed) return;
    try {
      await queue(holdMinutes);
    } catch (error) {
      if (closed) return;
      held = false;
      stopRenewal();
      onLost(error);
    }
  }

  return {
    get closed() {
      return closed;
    },
    get held() {
      return held && !closed;
    },
    async start() {
      if (closed) return false;
      requested = true;
      await queue(holdMinutes);
      // A late answer after the flow closed must not start renewing again.
      if (closed) return false;
      held = true;
      if (timer === undefined) timer = setInterval(() => void renew(), renewMs);
      return true;
    },
    close() {
      if (!tail) {
        closed = true;
        held = false;
        stopRenewal();
        // A failed tail is not reported: the device still ends the hold at its own time. A flow that
        // never asked for a pause has nothing to shorten.
        tail = requested
          ? queue(tailMinutes).catch(() => undefined)
          : Promise.resolve();
      }
      return tail;
    },
  };
}
