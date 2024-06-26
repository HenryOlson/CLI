/*
 * Copyright (c) 2013, Majenko Technologies
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 * 
 * * Redistributions of source code must retain the above copyright notice, this
 *   list of conditions and the following disclaimer.
 * 
 * * Redistributions in binary form must reproduce the above copyright notice, this
 *   list of conditions and the following disclaimer in the documentation and/or
 *   other materials provided with the distribution.
 * 
 * * Neither the name of Majenko Technologies nor the names of its
 *   contributors may be used to endorse or promote products derived from
 *   this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <CLI.h>

CLIServer CLI;

CLIServer::CLIServer() {
    clients = NULL;
    commands = NULL;
    prompt = NULL;
    _caseSensitive = true;
}

void CLIServer::setCaseInsensitive() {
    _caseSensitive = false;
}

void CLIServer::setCaseSensitive() {
    _caseSensitive = true;
}

boolean CLIServer::isCaseSensitive() {
    return _caseSensitive;
}


CLIClient *CLIServer::addClient(Stream &dev, void *data) {
    return addClient(&dev, data);
}

CLIClient *CLIServer::addClient(Stream &dev) {
    return addClient(&dev, NULL);
}

CLIClient *CLIServer::addClient(Stream *dev) {
    return addClient(dev, NULL);
}

CLIClient *CLIServer::addClient(Stream *dev, void *data) {
    CLIClientList *scan;
    CLIClientList *newClient;

    newClient = (CLIClientList *)malloc(sizeof(CLIClientList));

    newClient->client = new CLIClient(dev);
    newClient->client->setPrompt(prompt);
    newClient->client->setSessionData(data);
    newClient->next = NULL;
    if (clients == NULL) {
        clients = newClient;
        return newClient->client;
    }
    for (scan = clients; scan->next; scan = scan->next);
    scan->next = newClient;
    return newClient->client;
}

void CLIServer::addCommand(const char *command, int (*function)(CLIClient *, int, char **),
            const char* action, const char* usage, const char* help) {
    CLICommand *scan;
    CLICommand *newCommand;

    newCommand = (CLICommand *)malloc(sizeof(CLICommand));
    newCommand->command = command;
    newCommand->flags = 0;
    newCommand->function = function;
    newCommand->action = action;
    newCommand->usage = usage;
    newCommand->help = help;
    newCommand->next = NULL;

    if (commands == NULL) {
        commands = newCommand;
        return;
    }
    for (scan = commands; scan->next; scan = scan->next);
    scan->next = newCommand;
}

void CLIServer::addPrefix(const char *command, int (*function)(CLIClient *, int, char **),
            const char* action, const char* usage, const char* help) {
    CLICommand *scan;
    CLICommand *newCommand;

    newCommand = (CLICommand *)malloc(sizeof(CLICommand));
    newCommand->command = strdup(command);
    newCommand->flags = CLI_IS_PREFIX;
    newCommand->function = function;
    newCommand->action = action;
    newCommand->usage = usage;
    newCommand->help = help;
    newCommand->next = NULL;

    if (commands == NULL) {
        commands = newCommand;
        return;
    }
    for (scan = commands; scan->next; scan = scan->next);
    scan->next = newCommand;
}

static inline char *getWord(char *buf) {
    static char *ptr = NULL;
    char *start, *scan;
    char term = ' ';

    if (buf != NULL) {
        ptr = buf;
    }

    while ((*ptr == ' ' || *ptr == '\t') && *ptr != '\0') {
        ptr++;
    }
    if (*ptr == '\0') {
        return NULL;
    }

    if (*ptr == '"' || *ptr == '\'') {
        term = *ptr;
        ptr++;
    }
    start = ptr;

    while (*ptr != '\0') {
        if (*ptr == '\\') {
            for (scan = ptr; *scan != '\0'; scan++) {
                *scan = *(scan+1);
            }
            ptr++;
            continue;
        }
        if (*ptr == term || (term == ' ' && *ptr == '\t')) {
            *ptr = '\0';
            ptr++;
            return start;
        }
        ptr++;
    }
    if (ptr == start) {
        return NULL;
    }
    return start;
}

int CLIClient::readline() {
	int rpos;

    if (!dev->available()) {
        return -1;
    }

	int readch = dev->read();

	while (readch > 0) {
		switch (readch) {
			case '\r': // Return on CR, NL
			case '\n':
				rpos = pos;
				pos = 0;  // Reset position index ready for next time
				if (willEcho) dev->println();
                if (_redirect != NULL) {
                    _redirect(this, input, rpos);
                    return -1;
                }
				return rpos;
			case 8:
			case 127:
				if (pos > 0) {
					pos--;
					input[pos] = 0;
					if (willEcho) dev->print("\b \b");
				}
				break;
			default:
				if (pos < CLI_BUFFER-2) {
					if (willEcho) dev->write(readch);	
					input[pos++] = readch;
					input[pos] = 0;
				}
		}
        readch = dev->read();
	}
	// No end of line has been found, so return -1.
	return -1;
}

int CLIClient::parseCommand() {
    CLICommand *scan;
	char *argv[20];
	int argc;
	char *w;

	argc = 0;
	w = getWord(input);
	while ((argc < 20) && (w != NULL)) {
		argv[argc++] = w;
		w = getWord(NULL);
	}

    if(strcmp(argv[0], "help") == 0) {
        return CLI._showHelp(this, argc, argv);
    }

    if (CLI.isCaseSensitive()) {
        for (scan = CLI.commands; scan; scan = scan->next) {
            if ((scan->flags & CLI_IS_PREFIX) == 0) {
                if (strcmp(scan->command, argv[0]) == 0) {
                    return scan->function(this, argc, argv);
                }
            }
        }
        for (scan = CLI.commands; scan; scan = scan->next) {
            if ((scan->flags & CLI_IS_PREFIX) != 0) {
                if (strncmp(scan->command, argv[0], strlen(scan->command)) == 0) {
                    return scan->function(this, argc, argv);
                }
            }
        }
    } else {
        for (scan = CLI.commands; scan; scan = scan->next) {
            if ((scan->flags & CLI_IS_PREFIX) == 0) {
                if (strcasecmp(scan->command, argv[0]) == 0) {
                    return scan->function(this, argc, argv);
                }
            }
        }
        for (scan = CLI.commands; scan; scan = scan->next) {
            if ((scan->flags & CLI_IS_PREFIX) != 0) {
                if (strncasecmp(scan->command, argv[0], strlen(scan->command)) == 0) {
                    return scan->function(this, argc, argv);
                }
            }
        }
    }
	return -1;
}

void CLIClient::setSessionData(void *data) {
    _sessionData = data;
}

void *CLIClient::getSessionData() {
    return _sessionData;
}

void CLIServer::process() {
    CLIClientList *scan;
    for (scan = clients; scan; scan = scan->next) {
        uint8_t ctest = scan->client->testConnected();

        if (ctest == CLIClient::CONNECTED) {
            if (_onConnect != NULL) {
                _onConnect(scan->client, 0, NULL);
                scan->client->printPrompt();
            }
        } else if (ctest == CLIClient::DISCONNECTED) {
            if (_onDisconnect != NULL) {
                _onDisconnect(scan->client, 0, NULL);
            }
        }

        int rl = scan->client->readline();
        if (rl > 0) {
            int rv = scan->client->parseCommand();
            if (rv == -1) {
                scan->client->println("Unknown command");
            } else {
                if (rv > 0) {
                    scan->client->print("Error ");
                    scan->client->println(rv);
                }
            }
            scan->client->printPrompt();
        } else if (rl == 0) {
            scan->client->printPrompt();
        }
    }
}

#if (ARDUINO >= 100) 
size_t CLIServer::write(uint8_t c) {
#else
void CLIServer::write(uint8_t c) {
#endif
    CLIClientList *scan;
    for (scan = clients; scan; scan = scan->next) {
        scan->client->write(c);
    }
#if (ARDUINO > 100)
    return 1;
#endif
}

CLIClient::CLIClient(Stream *d) {
    dev = d;
    pos = 0;
    memset(input, 0, CLI_BUFFER);
    connected = false;
    willEcho = true;
    prompt = NULL;
    _redirect = NULL;
}

#if (ARDUINO >= 100) 
size_t CLIClient::write(uint8_t c) {
#else
void CLIClient::write(uint8_t c) {
#endif
    dev->write(c);
#if (ARDUINO > 100)
    return 1;
#endif
}

void CLIClient::echo(boolean e) {
    willEcho = e;
};

void CLIClient::setPrompt(const char *p) {
    if (prompt != NULL) {
        free(prompt);
        prompt = NULL;
    }
    if (p == NULL) {
        return;
    }
    prompt = strdup(p);
}

void CLIServer::setDefaultPrompt(const char *p) {
    if (prompt != NULL) {
        free(prompt);
        prompt = NULL;
    }
    if (p == NULL) {
        return;
    }
    prompt = strdup(p);
}

void CLIClient::printPrompt() {
    if (prompt == NULL) {
        return;
    }
    print(prompt);
}

uint8_t CLIClient::testConnected() {
    if (&dev) {
        if (!connected) {
            connected = true;
            return CLIClient::CONNECTED;
        }
    } else {
        if (connected) {
            connected = false;
            return CLIClient::DISCONNECTED;
        }
    }
    return CLIClient::IDLE;
}

void CLIServer::onConnect(int (*function)(CLIClient *, int, char **)) {
    _onConnect = function;
}

void CLIServer::onDisconnect(int (*function)(CLIClient *, int, char **)) {
    _onDisconnect = function;
}


CLIClient::~CLIClient() {
    if (prompt) {
        free(prompt);
    }
}

void CLIServer::removeClient(CLIClient &c) {
    removeClient(&c);
}

void CLIServer::removeClient(CLIClient *c) {
    removeClient(c->dev);
}

void CLIServer::removeClient(Stream &dev) {
    removeClient(&dev);
}

void CLIServer::removeClient(Stream *dev) {
    CLIClientList *scan;
    CLIClientList *oldClient;
    
    if (clients->client->dev == dev) {
        oldClient = clients;
        clients = oldClient->next;
        delete oldClient->client;
        free(oldClient);
        return;
    }

    for (scan = clients; scan->next; scan = scan->next) {
        if (scan->next->client->dev == dev) {
            oldClient = scan->next;
            scan->next = oldClient->next;
            delete oldClient->client;
            free(oldClient);
            return;
        }
    }
}

int CLIServer::_showHelp(CLIClient c, int argc, char** argv) {
    CLICommand *scan;
    char lineBuf[100];
    switch (argc) {
        case 1:
            // top-level help
            c.println("commands:");
            for (scan = CLI.commands; scan; scan = scan->next) {
                sprintf(lineBuf, "%-20s - ", scan->command);
                c.print(lineBuf);
                if(scan->action != NULL) {
                    c.print(scan->action);
                    if(scan->usage != NULL) {
                        c.print(", ");
                    }
                }
                if(scan->usage != NULL) {
                    c.print("usage: ");
                    c.print(scan->usage);
                }
                c.println();
            }
            c.println("type 'help {cmd}' for more specifics");
            return 0;
        case 2:
            for (scan = CLI.commands; scan; scan = scan->next) {
                if (strcmp(scan->command, argv[1]) == 0) {
                    c.print("Command: ");
                    c.print(scan->command);
                    if(scan->action != NULL) {
                        c.print(" - ");
                        c.println(scan->action);
                    } else {
                        c.println("");
                    }
                    if(scan->usage != NULL) {
                        c.print("Usage: ");
                        c.println(scan->usage);
                    }
                    if(scan->help != NULL) {
                        c.println(scan->help);
                    }
                    return 0;
                }
            }
            c.print(argv[0]);
            c.print(": ");
            c.print(argv[1]);
            c.println(" - unknown command");
            return 1;
        default:
            c.println("invalid help command");
            return 2;
    }
}
