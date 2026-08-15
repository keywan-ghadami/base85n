/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

/**
 * Error kinds for Base85N decoding failures (see spec Section 10).
 */
export type Base85NDecodeErrorCode =
  /** A character was encountered that is not part of Alphabet-N (after whitespace stripping). */
  | "invalid_character"
  /** EOF was reached when more characters were expected: mid group, mid signal, or while
   *  reading the data a DP signal declared. */
  | "unexpected_end_of_stream"
  /** A 5-character group's value fell in FUTURE_SIGNAL_SPACE, above every signal this
   *  version of the format defines. */
  | "undefined_signal"
  /** A trailing group of fewer than five characters was malformed: a single leftover
   *  character, a padded value that does not fit in 32 bits, or characters that are not
   *  the canonical encoding of the bytes they decode to. */
  | "invalid_final_block";

export interface Base85NDecodeErrorOptions {
  /** Index into the (whitespace-stripped) character stream where the problem was detected. */
  position?: number;
  cause?: unknown;
}

/**
 * Thrown by {@link decode} whenever the input string is not a valid Base85N encoding.
 */
export class Base85NDecodeError extends Error {
  /** Machine-readable classification of the failure, see {@link Base85NDecodeErrorCode}. */
  public readonly code: Base85NDecodeErrorCode;
  /** Index into the whitespace-stripped character stream where the problem was detected, if known. */
  public readonly position: number | undefined;

  constructor(code: Base85NDecodeErrorCode, message: string, options?: Base85NDecodeErrorOptions) {
    const positionSuffix = options?.position !== undefined ? ` (at position ${options.position})` : "";
    super(`${message}${positionSuffix}`, options?.cause !== undefined ? { cause: options.cause } : undefined);
    this.name = "Base85NDecodeError";
    this.code = code;
    this.position = options?.position;
    Object.setPrototypeOf(this, Base85NDecodeError.prototype);
  }
}
