/**
 * Copyright (C) 2007 Doug Judd (Zvents, Inc.)
 * 
 * This file is part of Hypertable.
 * 
 * Hypertable is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or any later version.
 * 
 * Hypertable is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include "Common/Error.h"
#include "Common/Exception.h"
#include "Common/StringExt.h"

#include "AsyncComm/ApplicationQueue.h"

#include "Hypertable/Lib/MasterProtocol.h"

#include "ConnectionHandler.h"
#include "RequestHandlerCreateTable.h"
#include "RequestHandlerGetSchema.h"
#include "RequestHandlerStatus.h"
#include "RequestHandlerRegisterServer.h"

using namespace hypertable;

/**
 *
 */
void ConnectionHandler::handle(EventPtr &eventPtr) {
  short command = -1;

  if (eventPtr->type == Event::MESSAGE) {
    ApplicationHandler *requestHandler = 0;

    //eventPtr->Display()

    try {
      if (eventPtr->messageLen < sizeof(int16_t)) {
	std::string message = "Truncated Request";
	throw new ProtocolException(message);
      }
      memcpy(&command, eventPtr->message, sizeof(int16_t));

      // sanity check command code
      if (command < 0 || command >= MasterProtocol::COMMAND_MAX) {
	std::string message = (std::string)"Invalid command (" + command + ")";
	throw ProtocolException(message);
      }

      switch (command) {
      case MasterProtocol::COMMAND_CREATE_TABLE:
	requestHandler = new RequestHandlerCreateTable(mComm, mMaster, eventPtr);
	break;
      case MasterProtocol::COMMAND_GET_SCHEMA:
	requestHandler = new RequestHandlerGetSchema(mComm, mMaster, eventPtr);
	break;
      case MasterProtocol::COMMAND_STATUS:
	requestHandler = new RequestHandlerStatus(mComm, mMaster, eventPtr);
	break;
      case MasterProtocol::COMMAND_REGISTER_SERVER:
	requestHandler = new RequestHandlerRegisterServer(mComm, mMaster, eventPtr);
	break;
      default:
	std::string message = (string)"Command code " + command + " not implemented";
	throw ProtocolException(message);
      }
      ApplicationHandlerPtr  appHandlerPtr( requestHandler );
      mAppQueue->Add( appHandlerPtr );
    }
    catch (ProtocolException &e) {
      ResponseCallback cb(mComm, eventPtr);
      std::string errMsg = e.what();
      LOG_VA_ERROR("Protocol error '%s'", e.what());
      cb.error(Error::PROTOCOL_ERROR, errMsg);
    }
  }
  else {
    LOG_VA_INFO("%s", eventPtr->toString().c_str());
  }

}

