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
  /** EOF was reached when more characters were expected (mid group, mid signal, or while
   *  reading a DP segment's declared transformed_DP_data). */
  | "unexpected_end_of_stream"
  /** An escape character ('~') was the last character available within a DP data segment. */
  | "dangling_escape_character"
  /** A DP signal's SignalPayload fell outside the valid 0..2^22-1 range. */
  | "reserved_signal_value"
  /** A trailing group of Alphabet-N characters did not form a valid partial final block
   *  (i.e. its length was not 0, or a multiple of 5, or 2/3/4 at the very end of the stream). */
  | "invalid_partial_block_length";

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
