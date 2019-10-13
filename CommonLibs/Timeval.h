/*
* Copyright 2008 Free Software Foundation, Inc.
*
* SPDX-License-Identifier: AGPL-3.0+
*
* This software is distributed under the terms of the GNU Affero Public License.
* See the COPYING file in the main directory for details.
*
* This use of this software may be subject to additional restrictions.
* See the LEGAL file in the main directory for details.

	This program is free software: you can redistribute it and/or modify
	it under the terms of the GNU Affero General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU Affero General Public License for more details.

	You should have received a copy of the GNU Affero General Public License
	along with this program.  If not, see <http://www.gnu.org/licenses/>.

*/


#ifndef TIMEVAL_H
#define TIMEVAL_H

#include <stdint.h>
#include "sys/time.h"
#include <iostream>
#include <unistd.h>



/** A wrapper on usleep to sleep for milliseconds. */
inline void msleep(long v) { usleep(v*1000); }


/** A C++ wrapper for struct timeval. */
class Timeval {

	private:

	struct timespec mTimespec;

	public:

	/** Set the value to current time. */
	void now();

	/** Set the value to gettimeofday plus an offset. */
	void future(unsigned ms);

	//@{
	Timeval(unsigned sec, unsigned usec)
	{
		mTimespec.tv_sec = sec;
		mTimespec.tv_nsec = usec*1000;
	}

	Timeval(const struct timeval& wTimeval)
	{
		mTimespec.tv_sec = wTimeval.tv_sec;
		mTimespec.tv_nsec = wTimeval.tv_sec*1000;
	}

	/**
		Create a Timespec offset into the future.
		@param offset milliseconds
	*/
	Timeval(unsigned offset=0) { future(offset); }
	//@}

	/** Convert to a struct timespec. */
	struct timespec timespec() const;

	/** Return total seconds. */
	double seconds() const;

	uint32_t sec() const { return mTimespec.tv_sec; }
	uint32_t usec() const { return mTimespec.tv_nsec / 1000; }
	uint32_t nsec() const { return mTimespec.tv_nsec; }

	/** Return difference from other (other-self), in ms. */
	long delta(const Timeval& other) const;

	/** Elapsed time in ms. */
	long elapsed() const { return delta(Timeval()); }

	/** Remaining time in ms. */
	long remaining() const { return -elapsed(); }

	/** Return true if the time has passed, as per clock_gettime(CLOCK_REALTIME). */
	bool passed() const;

	/** Add a given number of minutes to the time. */
	void addMinutes(unsigned minutes) { mTimespec.tv_sec += minutes*60; }

};

std::ostream& operator<<(std::ostream& os, const Timeval&);

std::ostream& operator<<(std::ostream& os, const struct timespec&);


#endif
// vim: ts=4 sw=4
