/**
 *  Class handling Agent module registered in iotinator master
 *  Xavier Grosjean 2018
 *  Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International Public License
 */

#include "AgentCollection.h"

AgentCollection::AgentCollection(XIOTModule* module) {
  _module = module;
  Debug("Agent count: %d\n", getCount());
}

int AgentCollection::getCount() {
  return _agents.size();
}

Agent* AgentCollection::refresh(char* jsonStr) {
  Debug("AgentCollection::refresh\n");
  StaticJsonBuffer<JSON_BUFFER_REGISTER_SIZE> jsonBuffer; // registration is bigger than needed
  JsonObject& root = jsonBuffer.parseObject(jsonStr); 
  if (!root.success()) {
    Serial.println("Refreshing parse failure for:");
    Serial.println(jsonStr);
    return NULL;
  }
  const char *mac = (const char*)root[XIOTModuleJsonTag::MAC];
  if(!mac) {
    Serial.println("Refreshing: missing MAC addr");
    return NULL;
  }
  _module->getDisplay()->setLine(1, "Refreshing", TRANSIENT, NOT_BLINKING);
  _module->getDisplay()->setLine(2, mac, TRANSIENT, NOT_BLINKING);
  
  agentMap::iterator it;
  it = _agents.find(mac);
  if(it == _agents.end()) {
    Serial.println("Refreshing: could not find module.");
    return NULL;
  }
  Agent *agent = it->second;
  _module->getDisplay()->setLine(2, agent->getName(), TRANSIENT, NOT_BLINKING);
  agent->setCustom((const char*)root[XIOTModuleJsonTag::custom]);
  return agent; // ptr to agent in collection, safe to return.
}

/**
 * Register a new agent
 * data from jsonStr needs to be copied, since it will be freed
 */ 
Agent* AgentCollection::add(char* jsonStr) {
  StaticJsonBuffer<JSON_BUFFER_REGISTER_SIZE> jsonBuffer; 
  JsonObject& root = jsonBuffer.parseObject(jsonStr); 
  if (!root.success()) {
    Serial.println("Registration parse failure for:");
    Serial.println(jsonStr);
    _module->sendJson("{}", 500);
    return NULL;
  }
  const char *name = (const char*)root[XIOTModuleJsonTag::name]; 
  const char *mac = (const char*)root[XIOTModuleJsonTag::MAC];
  const char *ip = (const char*)root[XIOTModuleJsonTag::ip];
  if(!name || !mac || !ip) {
    return NULL;
  }
  Debug("AgentCollection::add name '%s', mac '%s', ip '%s'\n", name, mac, ip);
  _module->getDisplay()->setLine(1, "Registering", TRANSIENT, NOT_BLINKING);
  _module->getDisplay()->setLine(2, name, TRANSIENT, NOT_BLINKING);
  Agent* agent = new Agent(name, mac, _module);
  // Insert it.
  std::pair <agentMap::iterator, bool> agentIt = _agents.insert(agentPair(mac, agent));
  // If not inserted because exists, point to the one already registered so that we can update it
  if(!agentIt.second) {
    delete agent;
    agent = agentIt.first->second;
  }
  // We need to update some fields...  
  agent->setCanSleep((bool)root[XIOTModuleJsonTag::canSleep]);
  agent->setCustom((const char*)root[XIOTModuleJsonTag::custom]);
  agent->setUiClassName((const char*)root[XIOTModuleJsonTag::uiClassName]);
  agent->setHeap((int32_t)root[XIOTModuleJsonTag::heap]);
  agent->setPingPeriod((int)root[XIOTModuleJsonTag::pingPeriod]);  // Will set it to 0 if absent

  agent->setIP(ip);
  
  agent->setName(name); // in case it's a new name for an already registered module.
  // check if one OTHER (not same mac) already registered module already has this name
  if(nameAlreadyExists(name, mac)) {
    // Renaming will occur later, not within this request processing
    agent->setToRename(true);
  }  
  _refreshListBufferSize();
  return agent;
}

/**
 * Compute the buffer size to hold one attribute, its value and json syntax elements, for all registered modules
 **/
int AgentCollection::_jsonAttributeSize(int moduleCount, const char *attrName, int valueSize) {
  // size of the value of the attribute + size of its name + 2 double quotes + semi colon + coma 
  return moduleCount * (valueSize + strlen(attrName) + 2 + 1 + 1);
}

/**
 * Compute the buffer size to hold the json string listing all registered agent modules
 **/
void AgentCollection::_refreshListBufferSize() {
  int moduleCount = getCount();
  _listBufferSize = LIST_BUFFER_SIZE;
  _listBufferSize += _jsonAttributeSize(moduleCount, XIOTModuleJsonTag::MAC, MAC_ADDR_MAX_LENGTH);
  _listBufferSize += _jsonAttributeSize(moduleCount, XIOTModuleJsonTag::name, NAME_MAX_LENGTH);
  _listBufferSize += _jsonAttributeSize(moduleCount, XIOTModuleJsonTag::ip, DOUBLE_IP_MAX_LENGTH);
  _listBufferSize += _jsonAttributeSize(moduleCount, XIOTModuleJsonTag::canSleep, 5);  // true or false
  _listBufferSize += _jsonAttributeSize(moduleCount, XIOTModuleJsonTag::pong, 5); // true or false
  _listBufferSize += _jsonAttributeSize(moduleCount, XIOTModuleJsonTag::uiClassName, UI_CLASS_NAME_MAX_LENGTH);
  _listBufferSize += _jsonAttributeSize(moduleCount, XIOTModuleJsonTag::heap, sizeof(uint32_t));
}

char* AgentCollection::list() {
  int size = getCount();
  Debug("AgentCollection::list %d agents\n", size);
  if(size == 0) {
    return strcpy((char*)malloc(3), "{}");  
  }
  
  // Size estimation: https://arduinojson.org/assistant/
  // TODO: update this when necessary : max 10 fields per agent
  const size_t bufferSize = size*JSON_OBJECT_SIZE(10) + JSON_OBJECT_SIZE(size);
  
  DynamicJsonBuffer jsonBuffer(bufferSize);
  JsonObject& root = jsonBuffer.createObject();
  int customSize = 0;
  
  for (agentMap::iterator it=_agents.begin(); it!=_agents.end(); ++it) {
    JsonObject& agent = root.createNestedObject(it->second->getMAC());
    agent[XIOTModuleJsonTag::name] = it->second->getName();
    agent[XIOTModuleJsonTag::ip] = it->second->getIP();
    agent[XIOTModuleJsonTag::canSleep] = (bool)it->second->getCanSleep();
    agent[XIOTModuleJsonTag::pong] = (bool)it->second->getPong();
    agent[XIOTModuleJsonTag::uiClassName] = it->second->getUiClassName();
    agent[XIOTModuleJsonTag::heap] = it->second->getHeap();
    char *custom = (char *)it->second->getCustom();
    if(custom != NULL) {
      agent[XIOTModuleJsonTag::custom] = custom;
      customSize = strlen(custom);    
    }
    Debug("Name '%s' on mac '%s'\n", it->second->getName(), it->second->getMAC());
  }
  
  // listBufferSize is updated when a agent registers
  int strBufferSize = _listBufferSize + customSize;
  char* strBuffer = (char *)malloc(strBufferSize); 
  root.printTo(strBuffer, strBufferSize-1);
  Debug("Reserved size: %d, actual size: %d\n", strBufferSize, strlen(strBuffer));
  return strBuffer;
}

void AgentCollection::reset() {
  int size = getCount();
  const char *ip, *name; 
  Serial.printf("AgentCollection::reset %d agents\n", size);
  for (agentMap::iterator it=_agents.begin(); it!=_agents.end(); ++it) {
    ip = it->second->getIP();
    name = it->second->getName();
    Serial.printf("Reset module '%s' on ip '%s'\n", name, ip);
    bool result = it->second->reset();
    if(result) {
      
    }
    
    Serial.printf("Result: %s\n", result?"ok":"nok");
  }
}

void AgentCollection::ping() {
  int size = getCount();
  Debug("AgentCollection::ping %d agents\n", size);
  bool canSleep;  // If true, must not be pinged
  const char *ip, *name;
  int pingPeriod;
  
  for (agentMap::iterator it=_agents.begin(); it!=_agents.end(); ++it) {
    ip = it->second->getIP();
    name = it->second->getName();
    canSleep = (bool)it->second->getCanSleep();
    pingPeriod = (bool)it->second->getPingPeriod();
    if(!canSleep && pingPeriod > 0) {
      Serial.printf("Ping module '%s' on ip '%s' pingPeriod %d\n", name, ip, pingPeriod);
      bool result = it->second->ping();
      Serial.printf("Connected: %s\n", result?"true":"false");
      if(!result) {
        char message[100];
        sprintf(message, "Ping failed: %s", name);
        _module->getDisplay()->setLine(1, message, TRANSIENT, NOT_BLINKING);      
      }
    } else {
      Serial.printf("Not pinging module '%s' on ip '%s': canSleep: %d, pingPeriod: %d\n", name, ip, canSleep, pingPeriod);
    }
  }
  uint32_t freeMem = system_get_free_heap_size();
  Serial.printf("Free heap mem: %d\n", freeMem);    
}


void AgentCollection::renameOne(Agent *agent) {
  Debug("AgentCollection::renameOne");
  int digit = 0;
  char alpha[NAME_MAX_LENGTH + 1];
  char newName[NAME_MAX_LENGTH + 1];

  bool ok = false;
  int i;
  strcpy(alpha, agent->getName());
  char *withUnderscore = strtok(alpha, "_");
  if(withUnderscore != NULL) {
    char *digitPtr = strtok(NULL, "_");
    if(digitPtr != NULL) {
       digit = atoi(digitPtr);
    }
  }
  
  while (!ok && strlen(newName) < NAME_MAX_LENGTH) {
    sprintf(newName, "%s_%d", alpha, ++digit);
    Debug("Testing name %s\n", newName);   
    if(!nameAlreadyExists(newName, agent->getMAC())) {
      ok = true;
    }   
  }
  if(!ok) {
    Serial.println("Can't find a non duplicated name");
  } else {
    agent->renameTo(newName);
  }
  
}

/**
 * check if a name exists in the collection on a different mac
 */
bool AgentCollection::nameAlreadyExists(const char* name, const char* mac) {
  for (agentMap::iterator it=_agents.begin(); it!=_agents.end(); ++it) {
    if((strcmp(it->second->getName(), name) == 0) && (strcmp(it->second->getMAC(), mac) != 0))  {
      Debug("Found duplicate %s on ip %s\n", name, it->second->getIP()); // ip is easier for debugging since it's displayed on modules
      return true;
    }
  }
  return false;
}