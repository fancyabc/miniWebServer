#include "webserver.h"


int main()
{
    WebServer server;

    server.init(8000, 1, true, 3306, "fancy", "mypass", "test", 8, 8, true, 100);

    server.start();
}