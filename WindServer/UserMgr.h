//
//	<one line to give the program's name and a brief idea of what it does.>
//	Copyright (C) 2020. Wenjin Yu. NetDragon. All rights reserved.
//
//	Created at 2020/5/12 17:07:48
//	Version 1.0
//
//	This program, including documentation, is protected by copyright controlled
//	by NetDragon. All rights are reserved.
//

#pragma once

#include <map>
#include <memory>
#include <json/json.h>
#include <Base/MsgType.h>
#include "Proxy.h"
using namespace std;
using namespace wind;

namespace wind {

class IMsgSender
{
public:
	IMsgSender() {}
	virtual ~IMsgSender() {}

	virtual void SendMsg(uint32 scId, EMsgType msgType, JValue& val) = 0;
};

class User
{
public:
	User(uint32 userId, uint32 scId, IMsgSender* msgSendPtr) : userId_(userId), scId_(scId), msgSendPtr_(msgSendPtr) {}
	
	uint32 UserId() { return userId_; }

	void OnLogin()
	{
		LogSave("server.log", "User on login: [%d][%d]", userId_, scId_);

		LoginAck();
	}

	void OnRelogin(uint32 scId)
	{
		LogSave("server.log", "User on relogin: [%d][%d][%d]", userId_, scId_, scId);

		if (scId_) {
			JValue val;
			msgSendPtr_->SendMsg(scId_, EMsgType::Reset, val);
		}

		scId_ = scId;
		LoginAck();
	}

	void OnLogout()
	{
		LogSave("server.log", "Logout user: [%d][%d]", userId_, scId_);
	}

	void OnMsg(uint32 scId, EMsgType msgType, JValue& val)
	{
		if (scId_ != scId) {
			LogSave("server.log", "Ignore message from different socket: [%d][%d][%d]", userId_, scId_, scId);
			return;
		}

		switch (msgType) {
		case EMsgType::Talk:
		{
			string content = val["content"].asString();

			LogSave("server.log", "User talk: %d %d %d %.6s...%.6s", userId_, scId_, content.length(),
				content.substr(0, 6).c_str(), content.substr(content.length() - 6, 6).c_str());

			JValue valAck;
			valAck["result"] = static_cast<uint32>(ETalk::Ok);
			valAck["content"] = content;
			msgSendPtr_->SendMsg(scId_, EMsgType::TalkAck, valAck);
		}
		break;
		default:
		{
		}
		break;
		}
	}

	void LoginAck()
	{
		JValue val;
		val["result"] = static_cast<uint32>(ELoginAck::Ok);
		val["userId"] = userId_;
		msgSendPtr_->SendMsg(scId_, EMsgType::LoginAck, val);
	}

	uint32 ScId() { return scId_; }

	uint32 userId_ = 0;
	uint32 scId_ = 0;
	IMsgSender* msgSendPtr_ = nullptr;
};
typedef std::shared_ptr<User> UserPtr;

class UserMgr : public IMsgSender
{
public:
	UserMgr(Proxy* proxyPtr) 
		: proxyPtr_(proxyPtr)
	{

	}

	void Start()
	{
		runThr_ = std::thread([this]()->void {
			while (true) {
				JMsgItemPtr msgItemPtr = nullptr;
				while (msgItemPtr = proxyPtr_->ReadUserMsg()) {
					auto& msgItem = *msgItemPtr;
					auto& val = msgItem.val_;

					switch (msgItem.msgType_) {
					case EMsgType::Login:
					{
						uint32 userId = val["userId"].asUInt();

						LogSave("server.log", "Login user: [%d][%d]", userId, msgItem.scId_);

						if (!users_.count(userId)) {
							UserPtr userPtr = make_shared<User>(userId, msgItem.scId_, this);
							users_.insert({ userId, userPtr });
							userPtr->OnLogin();
						}
						else {
							auto userPtr = users_.at(userId);
							userPtr->OnRelogin(msgItem.scId_);
						}
					}
					break;
					case EMsgType::Logout:
					{
						WD_IF (!msgItem.userId_)
							break;

						WD_IF (!users_.count(msgItem.userId_))
							break;
						auto userPtr = users_.at(msgItem.userId_);

						if (userPtr->ScId() != msgItem.scId_) {
							LogSave("server.log", "User logout with different socket id: [%d][%d][%d]", msgItem.userId_, userPtr->ScId(), msgItem.scId_);
							break;
						}

						LogSave("server.log", "Logout user: [%d][%d]", msgItem.userId_, msgItem.scId_);

						userPtr->OnLogout();

						users_.erase(msgItem.userId_);
					}
					break;
					default:
					{
						WD_IF (!msgItem.userId_)
							break;

						WD_IF (!users_.count(msgItem.userId_))
							break;

						auto userPtr = users_.at(msgItem.userId_);
						userPtr->OnMsg(msgItem.scId_, msgItem.msgType_, val);
					}
					break;
					}
				}

				Sleep(100);
			}
		});
	}

	virtual void SendMsg(uint32 scId, EMsgType msgType, JValue& val)
	{
		proxyPtr_->SendMsg(scId, msgType, val);
	}

	void LogoutUser(uint32 userId)
	{
		users_.erase(userId);
	}

private: 
	map<uint32, UserPtr> users_;
	Proxy* proxyPtr_;
	std::thread runThr_;
};

}