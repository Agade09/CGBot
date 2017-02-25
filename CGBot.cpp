#include <gloox/client.h>
#include <gloox/message.h>
#include <gloox/messagehandler.h>
#include <gloox/connectionlistener.h>
#include <gloox/mucroom.h>
#include <gloox/mucroomhandler.h>
#include <iostream>
#include <unordered_map>
#include <fstream>
#include <iomanip>
#include <random>
#include <algorithm>
#include <chrono>
#include <sstream>
#include <regex>
using namespace std;
using namespace gloox;
using namespace std::chrono;

constexpr char ConfigFileName[]{"config.txt"};
constexpr char Start_Str[]{""},End_Str[]{"\0"};
constexpr int N_Markov{2};//Markov chain length

default_random_engine generator{static_cast<unsigned int>(system_clock::now().time_since_epoch().count())};

ostream& operator<<(ostream& os, Message::MessageType type) {
    switch (type) {
        case Message::Chat:
            os << "Chat";
            break;
        case Message::Error:
            os << "Error";
            break;
        case Message::Groupchat:
            os << "Groupchat";
            break;
        case Message::Headline:
            os << "Headline";
            break;
        case Message::Normal:
            os << "Normal";
            break;
        case Message::Invalid:
            os << "Invalid";
            break;
        default:
            os << "unknown type";
            break;
    }
}

ostream& operator<<(ostream& os, const Message& stanza) {
    os << "type:'" << stanza.subtype() <<  "' from:'" << stanza.from().full() << "' body:'" << stanza.body() << "'";
    return os;
}

struct ChannelBot{
    string room_name,nickname;
    JID roomJID;
    MUCRoom* room=nullptr;
    unordered_map<string,unordered_map<string,int>> words;
    unordered_map<string,long> Total_Weights;
    vector<string> Ignored_Talkers;
    ChannelBot(const string &nick,const string &RName,const JID &RJID,const vector<string> &ITalkers):nickname{nick},room_name{RName},roomJID{RJID},Ignored_Talkers{ITalkers}{
        LearnFromLogs();
    }
    ~ChannelBot(){
        if(room!=nullptr){
            delete room;
        }
    }
    /********************************************************/
    //The methods below handle the talking and learning of the bot
    /********************************************************/
    inline string Next_Word(const string &s,size_t delim){//Return next word starting from position delim included
        size_t next_word_end{s.find_first_of(' ',delim)};
        return s.substr(delim,next_word_end-delim);
    }
    inline string Last_Words(const string &s,size_t delim){//Return last N_Markov words before position delim
        delim=s.find_last_not_of(' ',delim-1);
        size_t begin=delim;
        for(int i=0;i<N_Markov;++i){
            begin=begin==0?string::npos:s.find_last_of(' ',begin-1);
            if(begin==string::npos){//Not enough words, return everything
                return s.substr(0,delim+1);
            }
        }
        return s.substr(begin+1,delim-begin);
    }
    inline string Next_SubMessage(const string &prev){//Generate next part of message from the N_Markov words before it
        uniform_int_distribution<long> Word_Distrib(0,Total_Weights[prev]);
        long word{Word_Distrib(generator)};
        for(auto W:words[prev]){
            word-=W.second;
            if(word<=0){
                return W.first;
            }
        }
    }
    inline string Generate_Sentence(){//Generate sentence with a markov chain model
        string sentence;
        while(true){
            string next{Next_SubMessage(Last_Words(sentence,-1))};
            if(next==End_Str){
                return sentence;
            }
            sentence+=next+" ";
        }
        throw(0);
    }
    inline string talk(){
        return Generate_Sentence();
    }
    inline void Reinforce(const string &a,const string &b){//Reinforce the connection from a->b
        //cerr << "Reinforce " << a << " -> " << b << endl;
        ++words[a][b];
        ++Total_Weights[a];
    }
    inline string Remove_All_Words(const string &s,const string &sub){//Remove all words containing the substring sub
        return regex_replace(s,regex(sub,regex_constants::icase),"");
    }
    inline string Filter_Message(const string &mess){
        return Remove_All_Words(mess,nickname);
    }
    inline void Learn_From_Message(string mess){
        mess=Filter_Message(mess);
        size_t delim=mess.find_first_not_of(' ');
        if(delim==string::npos){//Message has no words
            return;
        }
        //cerr << "Original: " << mess << endl;
        Reinforce(Start_Str,Next_Word(mess,delim));
        delim=mess.find_first_not_of(' ',mess.find_first_of(' ',delim));
        while(delim!=string::npos){
            string prev{Last_Words(mess,delim)};
            string next{Next_Word(mess,delim)};
            //cerr << "Delim: " << delim << " " << prev << " -> " << next << endl;
            Reinforce(prev,next);
            delim=mess.find_first_not_of(' ',mess.find_first_of(' ',delim));
        }
        Reinforce(Last_Words(mess,-1),End_Str);
    }
    inline void Learn_From_Message(const Message &msg){
        string mess{msg.body()};
        mess.erase(0,mess.find(" : ")+3);//Get rid of "(HH/MM/SS) Username : "
        Learn_From_Message(mess);
    }
    inline void Log(const Message &msg){
        ofstream log_file("./Logs/"+room_name+".log",ios::app);
        time_t t = time(nullptr);
        tm ptm = *localtime(&t);
        string message_body{msg.body()};
        replace(message_body.begin(),message_body.end(),'\n',' ');
        log_file << "(" << put_time(&ptm,"%T") << ") " << msg.from().resource() <<  " : " << msg.body() << endl;
    }
    inline void LearnFromLogs(){
        ifstream logfile("./Logs/"+room_name+".log");
        if(!logfile){
            cerr << "Couldn't open log file: " << room_name+".log" << endl;
        }
        while(logfile){
            string line,username;
            getline(logfile,line);
            stringstream ss(line);
            ss >> username >> username;
            if(find(Ignored_Talkers.begin(),Ignored_Talkers.end(),username)==Ignored_Talkers.end()){
                line.erase(0,line.find(" : ")+3);//Get rid of "(HH/MM/SS) Username : "
                Learn_From_Message(line);
            }
        }
    }
};

struct Bot : public MessageHandler,ConnectionListener,MUCRoomHandler{
    Client* client;
    vector<ChannelBot> Channel;
    vector<string> Ignored_Talkers;
    int codingame_id,port;
    string password,host,MUC,nickname;
    Bot(){
        Read_Config_File();
        stringstream ss_client_jid;
        ss_client_jid << codingame_id << "@" << host;
        JID jid(ss_client_jid.str());
        client=new Client(jid,password,port);
        client->registerMessageHandler(this);
        client->registerConnectionListener(this);
        client->setPresence(Presence::Available,-1);
        for(ChannelBot &C:Channel){
            C.room=new MUCRoom(client,JID(C.room_name+"@"+MUC+"/"+C.nickname),this,0);
        }
        client->connect(true);
    }
    ~Bot(){
        delete client;
    }
    /********************************************************/
    //Reading parameter file
    /********************************************************/
    template <typename T> void GetParameterSkipLine(ifstream &config,T &param){
        string line;
        getline(config,line);
        stringstream ss(line);
        ss >> param;
    }
    void Read_Config_File(){
        ifstream config(ConfigFileName);
        GetParameterSkipLine(config,codingame_id);
        GetParameterSkipLine(config,password);
        GetParameterSkipLine(config,host);
        GetParameterSkipLine(config,port);
        GetParameterSkipLine(config,MUC);
        GetParameterSkipLine(config,nickname);
        string line;
        getline(config,line);
        stringstream ss_talkers(line);
        while(ss_talkers){
            string talker_name;
            ss_talkers >> talker_name;
            Ignored_Talkers.push_back(nickname);
            if(talker_name.find_first_not_of(' ')!=string::npos){
                Ignored_Talkers.push_back(talker_name);
            }
        }
        getline(config,line);
        stringstream ss_rooms(line);
        while(ss_rooms){
            string room_name;
            ss_rooms >> room_name;
            if(room_name.find_first_not_of(' ')!=string::npos){
                Channel.push_back(ChannelBot(nickname,room_name,JID(room_name+"@"+MUC),Ignored_Talkers));
            }
        }
    }
    /****************************************************/
    //The methods below handle events in the XMPP protocol
    /****************************************************/
    virtual void handleMUCMessage( MUCRoom* room, const Message& msg, bool priv ){
        if(!priv){
            for(ChannelBot &C:Channel){
                if(C.room_name==room->name()){
                    //cout <<  msg.from().resource() << ": " << msg.body() << endl;
                    if(!msg.when()){//If the message is new, that is to say not from history
                        if(msg.subtype()!=Message::Chat && msg.subtype()!=Message::Groupchat){
                            cerr << msg << endl;
                        }
                        if(find(Ignored_Talkers.begin(),Ignored_Talkers.end(),msg.from().resource())==Ignored_Talkers.end()){
                            C.Learn_From_Message(msg.body());
                        }
                        C.Log(msg);
                        if(regex_search(msg.body(),regex(nickname,regex_constants::icase))){
                            Message reply(Message::Groupchat,C.roomJID,C.talk());
                            reply.setID(C.room_name+"_"+nickname+"_"+to_string(system_clock::now().time_since_epoch().count()));
                            client->send(reply);
                        }
                    }
                }
            }
        }
        else{
            cout << "Private MUC Message from " << msg.from().full() << ": " << msg.body() << endl;
            Message reply(Message::Chat,msg.from(),Channel[0].talk());
            reply.setID(msg.from().username()+"_"+nickname+"_"+to_string(system_clock::now().time_since_epoch().count()));
            client->send(reply);
        }
    }
    virtual void onConnect(){
        cerr << "Connected" << endl;
        for(ChannelBot &C:Channel){
            C.room->join();
        }
    }
    virtual void handleMessage(const Message& msg, MessageSession* session=0){
        cerr << "Received PM from " << msg.from().full() << ": " << msg.body() << endl;
        if(msg.subtype()==Message::Chat){
            Message reply(Message::Chat,msg.from().full(),Channel[0].talk());
            reply.setID(msg.from().username()+"_"+nickname+"_"+to_string(system_clock::now().time_since_epoch().count()));
            client->send(reply);
        }
    }
    virtual void onDisconnect(ConnectionError e) {
        cerr << "ConnListener::onDisconnect() " << e << endl;
    }
    virtual bool onTLSConnect(const CertInfo& info) {
        cerr << "ConnListener::onTLSConnect()" << endl;
        return true;
    }
    virtual void handleLog(LogLevel level, LogArea area, const string &message){
        cerr << "Log: level: " << level << " area: " << area << ", " << message << endl;
    }
    virtual void handleMUCParticipantPresence( MUCRoom * room, const MUCRoomParticipant participant,const Presence& presence ){
        if( presence.presence() == Presence::Available ){
            //cerr << participant.nick->resource() << " is in the room too" << endl;
        }
        else if( presence.presence() == Presence::Unavailable){
            //cerr << participant.nick->resource() << " has left the room" << endl;
        }
        else{
            //cerr << "Presence of " << participant.nick->resource() << " is " << presence.presence() << endl;
        }
    }
    virtual void handleMUCSubject( MUCRoom * room, const std::string& nick, const std::string& subject ){
        if(nick.empty()){
            cerr << "Subject: " << subject << endl;
        }
        else{
            cerr << nick << " has set the subject to " << subject << endl;
        }
    }
    virtual void handleMUCError( MUCRoom * room, StanzaError error ){
        cerr << "!Error: " << error << endl;
    }
    virtual void handleMUCInfo( MUCRoom * room, int features, const std::string& name,const DataForm* infoForm ){
        cerr << "features: " << features << " name: " << name << " form xml: " << infoForm->tag()->xml() << endl;
    }
    virtual void handleMUCItems(MUCRoom * room,const Disco::ItemList& items){
        for(Disco::ItemList::const_iterator it=items.begin();it!=items.end();++it){
            cerr << (*it)->jid().full() << " -- " << (*it)->name() << " is an item here" << endl;
        }
    }
    virtual void handleMUCInviteDecline( MUCRoom * room, const JID& invitee, const std::string& reason ){
      cerr << "Invitee " << invitee.full() << " declined invitation. Reason given: " << reason << endl;
    }
    virtual bool handleMUCRoomCreation( MUCRoom *room ){
      cerr << "Room " << room->name() << " didn't exist, being created." << endl;
      return true;
    }
};

int main(){
    Bot b;
}