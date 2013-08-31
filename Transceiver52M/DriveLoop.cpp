/*
* Copyright 2008, 2009, 2010, 2012 Free Software Foundation, Inc.
* Copyright 2013 Alexander Chemeris <Alexander.Chemeris@fairwaves.ru>
*
* This software is distributed under the terms of the GNU Public License.
* See the COPYING file in the main directory for details.
*
* This use of this software may be subject to additional restrictions.
* See the LEGAL file in the main directory for details.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <stdio.h>
#include "DriveLoop.h"
#include <Logger.h>
#include "RTMD.h"

using namespace GSM;

DriveLoop::DriveLoop(int wBasePort, const char *TRXAddress,
                     RadioInterface *wRadioInterface,
		     int wChanM, int wC0, int wSamplesPerSymbol,
                     GSM::Time wTransmitLatency)
: Thread("DriveLoop")
, mClockSocket(wBasePort, TRXAddress, wBasePort + 100)
, mC0(wC0)
{
  mChanM = wChanM;
  mSamplesPerSymbol = wSamplesPerSymbol;
  mRadioInterface = wRadioInterface;

  mStartTime = (random() % gHyperframe, 0);

  mTransmitDeadlineClock = mStartTime;
  mLatencyUpdateTime = mStartTime;
  mTransmitLatency = wTransmitLatency;
  mLastClockUpdateTime = mStartTime;

  mRadioInterface->getClock()->set(mStartTime);

  // generate pulse and setup up signal processing library
  gsmPulse = generateGSMPulse(2, mSamplesPerSymbol);
  LOG(DEBUG) << "gsmPulse: " << *gsmPulse;
  sigProcLibSetup(mSamplesPerSymbol);

  txFullScale = mRadioInterface->fullScaleInputValue();

  // initialize filler tables with dummy bursts on C0, empty bursts otherwise
  for (int i = 0; i < 8; i++) {
    signalVector* modBurst = modulateBurst(gDummyBurst, *gsmPulse,
                                           8 + (i % 4 == 0), mSamplesPerSymbol);
    scaleVector(*modBurst, txFullScale);
    for (int j = 0; j < 102; j++) {
      for (int n = 0; n < mChanM; n++) {
        if (n == mC0)
          fillerTable[n][j][i] = new signalVector(*modBurst);
        else
          fillerTable[n][j][i] = new signalVector(modBurst->size());
      }
    }
    delete modBurst;

    for (int n = 0; n < mChanM; n++) {
      fillerModulus[n][i] = 26;
      mChanType[n][i] = NONE;
    }
  }
}

DriveLoop::~DriveLoop()
{
  stopThread();
  delete gsmPulse;
  sigProcLibDestroy();
}

void DriveLoop::pushRadioVector(GSM::Time &nowTime)
{
  int i;
  radioVector *staleBurst;
  radioVector *next;
  RTMD_SET("pushRadioVector");

  for (i = 0; i < mChanM; i++) {
    // dump stale bursts, if any
    while (staleBurst = mTransmitPriorityQueue[i].getStaleBurst(nowTime)) {
      // Even if the burst is stale, put it in the fillter table.
      // (It might be an idle pattern.)
      LOG(NOTICE) << "dumping STALE burst in TRX->USRP interface";
    }

    int TN = nowTime.TN();
    int modFN = nowTime.FN() % fillerModulus[i][nowTime.TN()];

    mTxBursts[i] = fillerTable[i][modFN][TN];
    mIsFiller[i] = true;
    mIsZero[i] = (mChanType[i][TN] == NONE);

    // if queue contains data at the desired timestamp, stick it into FIFO
    if (next = (radioVector*) mTransmitPriorityQueue[i].getCurrentBurst(nowTime)) {
      LOG(DEBUG) << "transmitFIFO: wrote burst " << next << " at time: " << nowTime;
      mTxBursts[i] = next;
      mIsFiller[i] = false;
      mIsZero[i] = false;
    }
  }

  mRadioInterface->driveTransmitRadio(mTxBursts, mIsZero);

  for (i = 0; i < mChanM; i++) {
    if (!mIsFiller[i])
      delete mTxBursts[i];
  }
  RTMD_CLEAR("pushRadioVector");
}

void DriveLoop::setModulus(int channel, int timeslot)
{
  switch (mChanType[channel][timeslot]) {
  case NONE:
  case I:
  case II:
  case III:
  case FILL:
    fillerModulus[channel][timeslot] = 26;
    break;
  case IV:
  case VI:
  case V:
    fillerModulus[channel][timeslot] = 51;
    break;
    //case V: 
  case VII:
    fillerModulus[channel][timeslot] = 102;
    break;
  default:
    break;
  }
}

DriveLoop::CorrType DriveLoop::expectedCorrType(int channel, GSM::Time currTime)
{
  unsigned burstTN = currTime.TN();
  unsigned burstFN = currTime.FN();

  switch (mChanType[channel][burstTN]) {
  case NONE:
    return OFF;
    break;
  case FILL:
    return IDLE;
    break;
  case I:
    return TSC;
    /*if (burstFN % 26 == 25) 
      return IDLE;
    else
      return TSC;*/
    break;
  case II:
    if (burstFN % 2 == 1)
      return IDLE;
    else
      return TSC;
    break;
  case III:
    return TSC;
    break;
  case IV:
  case VI:
    return RACH;
    break;
  case V: {
    int mod51 = burstFN % 51;
    if ((mod51 <= 36) && (mod51 >= 14))
      return RACH;
    else if ((mod51 == 4) || (mod51 == 5))
      return RACH;
    else if ((mod51 == 45) || (mod51 == 46))
      return RACH;
    else
      return TSC;
    break;
  }
  case VII:
    if ((burstFN % 51 <= 14) && (burstFN % 51 >= 12))
      return IDLE;
    else
      return TSC;
    break;
  case LOOPBACK:
    if ((burstFN % 51 <= 50) && (burstFN % 51 >=48))
      return IDLE;
    else
      return TSC;
    break;
  default:
    return OFF;
    break;
  }
}
 
void DriveLoop::driveReceiveFIFO() 
{
  SoftVector *rxBurst = NULL;
  int RSSI;
  int TOA;  // in 1/256 of a symbol
  GSM::Time burstTime;

  mRadioInterface->driveReceiveRadio();
}

/*
 *  Features a carefully controlled latency mechanism, to
 *  assure that transmit packets arrive at the radio/USRP
 *  before they need to be transmitted.
 *  
 *  Deadline clock indicates the burst that needs to be
 *  pushed into the FIFO right NOW.  If transmit queue does
 *  not have a burst, stick in filler data.
 */
void DriveLoop::driveTransmitFIFO() 
{
  int i;
  GSM::Time timeDiff;

  RadioClock *radioClock = (mRadioInterface->getClock());
  timeDiff = radioClock->get() + mTransmitLatency - mTransmitDeadlineClock;
  while (timeDiff > 0) {
    RTMD_VAL("DrvTxFIFO-TDiff", timeDiff.FN()*8+timeDiff.TN());
    pushRadioVector(mTransmitDeadlineClock);
    mTransmitDeadlineClock.incTN();
    timeDiff = radioClock->get() + mTransmitLatency - mTransmitDeadlineClock;
  }

  // FIXME -- This should not be a hard spin.
  // But any delay here causes us to throw omni_thread_fatal.
  //else radioClock->wait();
}

void DriveLoop::writeClockInterface()
{
  RTMD_SET("TRX-writeClk");
  char command[50];
  // FIXME -- This should be adaptive.
  sprintf(command,"IND CLOCK %llu",
          (unsigned long long) (mTransmitDeadlineClock.FN() + 2));

  LOG(INFO) << "ClockInterface: sending " << command;

  mClockSocket.write(command,strlen(command)+1);

  mLastClockUpdateTime = mTransmitDeadlineClock;

  RTMD_CLEAR("TRX-writeClk");
}

void DriveLoop::runThread()
{
  setPriority();

  while (isThreadRunning()) {
    driveReceiveFIFO();
    driveTransmitFIFO();
  }
}
