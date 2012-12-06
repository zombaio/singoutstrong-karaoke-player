#include "httphandler.h"

namespace SoS
{
	namespace QtGui
	{

		HttpHandler::HttpHandler(QObject *parent) :
			QObject(parent)
		{
			connect(&networkManager, SIGNAL(finished(QNetworkReply*)),
					this, SLOT(handleResponse(QNetworkReply*)));
		}

		void HttpHandler::sendRequest(RequestType type, QUrl url)
		{
			QNetworkRequest request(url);
			request.setHeader(QNetworkRequest::ContentTypeHeader,"application/x-www-form-urlencoded");

			if(type == REQUEST_POST) networkManager.post(request, url.encodedQuery());
			else networkManager.get(request);
		}

		void HttpHandler::handleResponse(QNetworkReply *networkReply)
		{
			QUrl url = networkReply->url();
			if (!networkReply->error()) {
				response = networkReply->readAll();
				emit receivedResponse(response);
			}
			else
				emit responseError(networkReply->errorString());

			networkReply->deleteLater();
		}

	}
}
