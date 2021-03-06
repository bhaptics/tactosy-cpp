#ifndef TACTOSY_HPP
#define TACTOSY_HPP

#include "thirdparty/easywsclient.hpp"
#include "thirdparty/json.hpp"
#include "common/timer.hpp"
#include "common/model.hpp"
#include "common/util.hpp"

#ifdef _WIN32
#pragma comment( lib, "ws2_32" )
#include <WinSock2.h>
#endif
#include <assert.h>

namespace tactosy
{
#define MIN(X,Y) ((X) < (Y) ? (X) : (Y))  
#define MAX(X,Y) ((X) > (Y) ? (X) : (Y))  
    using namespace std;
    using easywsclient::WebSocket;
    class HapticPlayer
    {
		static HapticPlayer *hapticManager;
        unique_ptr<WebSocket> ws;

        map<string, BufferedHapticFeedback> _registeredSignals;
        map<string, BufferedHapticFeedback> _activeSignals;
        mutex mtx;
        int _currentTime = 0;
        int _interval = 20;
        int _motorSize = 20;
        TactosyTimer timer;

		bool _enable = false;

        string host = "127.0.0.1";
        int port = 15881;
        string path = "feedbacks";

        int reconnectSec = 5;
        std::chrono::steady_clock::time_point prevReconnect;

		HapticPlayer() {}

		//HapticPlayer(HapticPlayer const&);
		//void operator=(HapticPlayer const&);

        void reconnect()
        {
            if (!retryConnection)
            {
                return;
            }

            if (connectionCheck())
            {
                return;
            }

            std::chrono::steady_clock::time_point current = std::chrono::steady_clock::now();

            int values = std::chrono::duration_cast<std::chrono::seconds>(current - prevReconnect).count();

            if (values > reconnectSec)
            {
                auto tried = unique_ptr<WebSocket>(WebSocket::create(host, port, path));
                if (tried)
                {
					tried->close();
					tried->poll();
                    ws.reset(WebSocket::create(host, port, path));
                }
                prevReconnect = current;
            }
        }

        void playFeedback(const HapticFeedback &feedback)
        {
            vector<uint8_t> message(_motorSize + 2, 0);
            if (feedback.mode == PATH_MODE)
            {
                message[0] = 1;
            }
            else if (feedback.mode == DOT_MODE)
            {
                message[0] = 2;
            }

            message[1] = (uint8_t)feedback.position;

            for (int i = 0; i < _motorSize; i++)
            {
                message[i + 2] = feedback.values[i];
            }

            send(message);
        }

		void playFeedbackFrame(const HapticFeedbackFrame &feedback)
		{
			json j;

			if (!_enable)
			{
				vector<DotPoint> emptyPoints;
				HapticFeedbackFrame turnOffFrame(feedback.position,emptyPoints);
				tactosy::to_json(j, turnOffFrame);
			}
			else
			{
				tactosy::to_json(j, feedback);
			}

			sendStr(j.dump());
		}

        bool connectionCheck()
        {
            if (!ws)
            {
                return false;
            }

            if (ws->getReadyState() == WebSocket::CLOSED)
            {
                ws.reset();
                return false;
            }

            return true;
        }

        void send(const vector<uint8_t> &message)
        {
            if (!connectionCheck())
            {
                return;
            }
            ws->sendBinary(message);
            ws->poll();
        }

		void sendStr(const std::string &message)
		{
			if (!connectionCheck())
			{
				return;
			}

			ws->send(message);
			ws->poll();

		}

        void updateActive(const string &key, const BufferedHapticFeedback& signal)
        {
            mtx.lock();
            _activeSignals[key] = signal;
            mtx.unlock();
        }

        void remove(const string &key)
        {
            mtx.lock();
            _activeSignals.erase(key);
            mtx.unlock();
        }

        void removeAll()
        {
            mtx.lock();
            _activeSignals.clear();
            mtx.unlock();
        }

        void doRepeat()
        {
            reconnect();

			checkMessage();

            if (_activeSignals.size() == 0)
            {
                if (_currentTime > 0)
                {
                    _currentTime = 0;
                    //vector<uint8_t> bytes(_motorSize, 0);
                    //HapticFeedback rightFeedback(Right, bytes, DOT_MODE);
                    //HapticFeedback leftFeedback(Left, bytes, DOT_MODE);
                    //HapticFeedback vestFrontfeedback(VestFront, bytes, DOT_MODE);
                    //HapticFeedback vestBackFeedback(VestBack, bytes, DOT_MODE);
                    //HapticFeedback headFeedback(Head, bytes, DOT_MODE);
                    //playFeedback(rightFeedback);
                    //playFeedback(leftFeedback);
                    //playFeedback(vestFrontfeedback);
                    //playFeedback(vestBackFeedback);
                    //playFeedback(headFeedback);

					vector<DotPoint> points;
					HapticFeedbackFrame rightFeedback(Right, points);
					HapticFeedbackFrame leftFeedback(Left, points);
					HapticFeedbackFrame vestFrontfeedback(VestFront, points);
					HapticFeedbackFrame vestBackFeedback(VestBack, points);
					HapticFeedbackFrame headFeedback(Head, points);
					playFeedbackFrame(rightFeedback);
					playFeedbackFrame(leftFeedback);
					playFeedbackFrame(vestFrontfeedback);
					playFeedbackFrame(vestBackFeedback);
					playFeedbackFrame(headFeedback);
                }

                return;
            }

            vector<uint8_t> dotModeSignalLeft(_motorSize, 0);
            vector<uint8_t> dotModeSignalRight(_motorSize, 0);
            vector<uint8_t> pathModeSignalLeft(_motorSize, 0);
            vector<uint8_t> pathModeSignalRight(_motorSize, 0);

            vector<string> expiredSignals;

            bool dotModeLeftActive = false;
            bool dotModeRightActive = false;
            bool pathModeActiveLeft = false;
            bool pathModeActiveRight = false;

            map<Position, vector<HapticFeedback>> map;
            vector<HapticFeedback> left, right, vFront, vBack, head;
            map[Left] = left;
            map[Right] = right;
            map[VestFront] = vFront;
            map[VestBack] = vBack;
            map[Head] = head;

            mtx.lock();
            for (auto keyPair = _activeSignals.begin(); keyPair != _activeSignals.end(); ++keyPair)
            {
                auto key = keyPair->first;

                if (keyPair->second.StartTime > _currentTime || keyPair->second.StartTime < 0)
                {
                    keyPair->second.StartTime = _currentTime;
                }

                int timePast = _currentTime - keyPair->second.StartTime;

                if (timePast > keyPair->second.EndTime)
                {
                    expiredSignals.push_back(key);
                }
                else
                {
                    if (Common::containsKey(timePast, keyPair->second.feedbackMap))
                    {
                        auto hapticFeedbackData = keyPair->second.feedbackMap.at(timePast);
                        for (auto &feedback : hapticFeedbackData)
                        {
                            map[feedback.position].push_back(feedback);
                        }
                    }
                }
            }

			//convert HapticFeedback into aggregated HapticFeedbackFrame
            for (auto keyPair = map.begin(); keyPair != map.end(); ++keyPair)
            {
                auto pos = keyPair->first;
                auto feeds = keyPair->second;
                vector<uint8_t> values(_motorSize, 0);
                vector<uint8_t> path_values(_motorSize, 0);
				vector<DotPoint> dotPoints;
				vector<PathPoint> pathPoints;

                bool isDot = false;
                bool isPath = false;
                for(auto &feed : feeds)
                {
                    auto mode = feed.mode;

                    if (mode == DOT_MODE)
                    {
                        for (int i = 0; i < _motorSize; i++)
                        {
                            values[i] += feed.values[i];

                            if (values[i] > 100)
                            {
                                values[i] = 100;
                            }

							if (values[i] > 0)
							{
								DotPoint tempPoint(i, values[i]);
								dotPoints.push_back(tempPoint);
							}
                        }
                        isDot = true;
                    } else if (mode == PATH_MODE)
                    {
                        int prevSize = path_values[0];
                        
                        vector<uint8_t> data = feed.values;
                        int size = data[0];
                        if (prevSize + size > 6)
                        {
                            continue;
                        }
                        
                        path_values[0] = prevSize + size;
                        
                        for (int i = prevSize; i < prevSize + size; i++)
                        {
                            path_values[3 * i + 1] = data[3 * (i - prevSize) + 1];
                            path_values[3 * i + 2] = data[3 * (i - prevSize) + 2];
                            path_values[3 * i + 3] = data[3 * (i - prevSize) + 3];
							PathPoint tempPath((float)(data[3 * (i - prevSize) + 1])/40.0,(float)(data[3 * (i - prevSize) + 2])/30.0, data[3 * (i - prevSize) + 3]);
							pathPoints.push_back(tempPath);
                        }

                        isPath = true;
                    }
                }

				HapticFeedbackFrame feedbackFrame(pos, dotPoints);
				feedbackFrame.pathPoints = pathPoints;

				playFeedbackFrame(feedbackFrame);
				/*
                if (isDot)
                {
                    HapticFeedback feedback1(pos, values, DOT_MODE);
                    playFeedback(feedback1);
                }
                
                if (isPath)
                {
                    HapticFeedback feedback2(pos, path_values, PATH_MODE);
                    playFeedback(feedback2);

                }

                if (!isDot && !isPath)
                {
                    HapticFeedback feedback1(pos, values, DOT_MODE);
                    playFeedback(feedback1);
                }
				//*/
            }

            mtx.unlock();

            for (auto &key : expiredSignals)
            {
                remove(key);
            }

            _currentTime += _interval;
        }

        void callbackFunc()
        {
            doRepeat();
        }


    public:
        bool retryConnection = false;

        int registerFeedback(const string &key, const string &path)
        {
            try
            {
                TactosyFile file = Util::parse(path);
                BufferedHapticFeedback signal(file);
                _registeredSignals[key] = signal;

                return 0;
            } catch(exception &e)
            {
                printf("Exception : %s\n", e.what());

                return -1;
            }
        }

        void init()
        {
			if (_enable)
				return;
            function<void()> callback = std::bind(&HapticPlayer::callbackFunc, this);
            timer.addTimerHandler(callback);
            timer.start();

#ifdef _WIN32
            INT rc;
            WSADATA wsaData;

            rc = WSAStartup(MAKEWORD(2, 2), &wsaData);
            if (rc) {
                printf("WSAStartup Failed.\n");
                return;
            }
#endif

            try
            {
                ws = unique_ptr<WebSocket>(WebSocket::create(host, port, path));

                connectionCheck();
            }
            catch (exception &e)
            {
                printf("Exception : %s\n", e.what());
            }

            /*vector<uint8_t> values(_motorSize, 0);
            HapticFeedback feedback(Right, values, DOT_MODE);
            playFeedback(feedback);*/
			_enable = true;
			vector<DotPoint> points;
			HapticFeedbackFrame feedback(Right,points);
			playFeedbackFrame(feedback);
        }

        void submit(const string &key, Position position, const vector<uint8_t> &motorBytes, int durationMillis)
        {
            HapticFeedback feedback(position, motorBytes, DOT_MODE);
            BufferedHapticFeedback signal(feedback, durationMillis, _interval);
            updateActive(key, signal);
        }

		void submit(const string &key, Position position, const vector<DotPoint> &points, int durationMillis)
		{
			vector<uint8_t> bytes(_motorSize, 0);
			for (int i = 0; i < points.size();i++)
			{
				bytes[points[i].index] = points[i].intensity;
			}
			HapticFeedback feedback(position, bytes, DOT_MODE);
			BufferedHapticFeedback signal(feedback, durationMillis, _interval);
			updateActive(key, signal);
		}

        void submit(const string &key, Position position, const vector<PathPoint> &points, int durationMillis)
        {
            if (points.size() > 6 || points.size() <= 0)
            {
                printf("number of points should be [1 ~ 6]\n");
                return;
            }

            vector<uint8_t> bytes(_motorSize, 0);
            bytes[0] = points.size();
            for (size_t i = 0; i < points.size(); i++)
            {
                bytes[3 * i + 1] = static_cast<int>(MIN(40, MAX(0, points[i].x * 40))); // x
                bytes[3 * i + 2] = static_cast<int>(MIN(30, MAX(0, points[i].y * 30)));
                bytes[3 * i + 3] = static_cast<int>(MIN(100, MAX(0, points[i].intensity)));
            }

            HapticFeedback feedback(position, bytes, PATH_MODE);
            BufferedHapticFeedback signal(feedback, durationMillis, _interval);
            updateActive(key, signal);
        }

        void submitRegistered(const string &key, float intensity, float duration)
        {
            if (!Common::containsKey(key, _registeredSignals))
            {
                printf("Key : %s is not registered.\n", key.c_str());

                return;
            }

            if (duration < 0.01f || duration > 100.0f)
            {
                printf("not allowed duration %f\n", duration);
                return;
            }

            if (intensity < 0.01f || intensity > 100.0f)
            {
                printf("not allowed intensity %f\n", duration);
                return;
            }

            BufferedHapticFeedback signal = _registeredSignals.at(key);

            BufferedHapticFeedback copiedFeedbackSignal = BufferedHapticFeedback::Copy(signal, _interval, intensity, duration);
            updateActive(key, copiedFeedbackSignal);
        }

        void submitRegistered(const string &key)
        {
            if (!Common::containsKey(key, _registeredSignals))
            {
                printf("Key : '%s' is not registered.\n", key.c_str());

                return;
            }

            auto signal = _registeredSignals.at(key);

            signal.StartTime = -1;
            if (!Common::containsKey(key, _activeSignals))
            {
                updateActive(key, signal);
            } else
            {
                _activeSignals.at(key).StartTime = -1;
            }
            
        }

        bool isPlaying()
        {
            return _activeSignals.size() > 0;
        }

        bool isPlaying(const string &key)
        {
            return Common::containsKey(key, _activeSignals);
        }

        void turnOff()
        {
            removeAll();
        }

        void turnOff(const string &key)
        {
            if (!Common::containsKey(key, _activeSignals))
            {
                printf("feedback with key( %s ) is not playing.\n", key.c_str());
                return;
            }

            remove(key);
        }

		static void tempDispatch(const std::string& message)
		{
			printf(message.c_str());
		}

		static void tempDispatchChar(const char* message)
		{
			printf(message);
		}

		void(*dispatchFunctionVar)(const char*);

		void checkMessage()
		{
			//ws->dispatch(tempDispatch);
			//ws->dispatchChar(tempDispatchChar);
			if (!ws)
			{
				return;
			}
			if (!_enable)
			{
				return;
			}

			if (dispatchFunctionVar != NULL)
			{
				ws->dispatchChar(dispatchFunctionVar);
			}
			ws->poll();
		}

        void destroy()
        {
            //vector<uint8_t> values(_motorSize, 0);
            //HapticFeedback feedback(All, values, DOT_MODE);
            //playFeedback(feedback);

			vector<DotPoint> points;
			HapticFeedbackFrame feedback(All,points);
			playFeedbackFrame(feedback);
			_enable = false;
			
            if (!ws)
            {
                return;
            }

			ws->close();
			ws->poll();
			ws.reset();
			//delete ws;
        }

		HapticPlayer(HapticPlayer const&) = delete;
		void operator= (HapticPlayer const&) = delete;

		static HapticPlayer *instance()
		{
			if (!hapticManager)
			{
				hapticManager = new HapticPlayer();
			}

			return hapticManager;
		}
    };
}

#endif