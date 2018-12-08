#include <gloox/gloox.h>
#include <gloox/client.h>
#include <gloox/message.h>
#include <gloox/messagehandler.h>
#include <gloox/messagesession.h>
#include <gloox/messagesessionhandler.h>
#include <gloox/connectionlistener.h>
#include <gloox/mucroom.h>
#include <gloox/mucroomhandler.h>
#include <iostream>
#include <unordered_map>
#include <map>
#include <set>
#include <fstream>
#include <iomanip>
#include <random>
#include <algorithm>
#include <chrono>
#include <sstream>
#include <regex>
#include <filesystem>
namespace fs = std::filesystem;
using namespace std;
using namespace gloox;
using namespace std::chrono;

constexpr char ConfigFileName[]{"config.txt"};
constexpr char Start_Str[]{""},End_Str[]{"\0"};
constexpr int N_Markov{2};//Markov chain length
constexpr int Occurence_Limit{2};//Minimum occurences to accept a certain chain length when speaking

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
    return os;
}

ostream& operator<<(ostream& os, const Message& stanza) {
    os << "type:'" << stanza.subtype() <<  "' from:'" << stanza.from().full() << "' body:'" << stanza.body() << "'";
    return os;
}

inline int Words(const string &s){
    stringstream ss(s);
    int words{0};
    string word;
    while(ss >> word){
        ++words;
    }
    return words;
}

struct next_words{
    map<string,int> next;
    long total_weights;
};

struct ChannelBot{
    string room_name,nickname;
    JID roomJID;
    string MUC;
    MUCRoom* room{nullptr};
    unordered_map<string,next_words> words;
    set<string> Ignored_Talkers;
    ChannelBot(const string &nick,const string &RName,const string &MUC_Name,const JID &RJID,const set<string> &ITalkers):nickname{nick},room_name{RName},MUC{MUC_Name},roomJID{RJID},Ignored_Talkers{ITalkers}{
        time_point<system_clock> Start_Time{system_clock::now()};
        LearnFromLogs();
        cerr << nickname << " took " << static_cast<duration<double>>(system_clock::now()-Start_Time).count() << "s to process the logs of room " << room_name << endl;
    }
    ~ChannelBot(){
        if(room!=nullptr){
            delete room;
        }
    }
    /********************************************************/
    //The methods below handle the talking and learning of the bot
    /********************************************************/
    inline string Next_Word(const string &s,size_t delim)const{//Return next word starting from position delim included
        size_t next_word_end{s.find_first_of(' ',delim)};
        return s.substr(delim,next_word_end-delim);
    }
    inline string Last_Words(const string &s,const int N,size_t delim)const{//Return last N_Markov words before position delim
        delim=s.find_last_not_of(' ',delim-1);
        size_t begin=delim;
        for(int i=0;i<N;++i){
            begin=begin==0?string::npos:s.find_last_of(' ',begin-1);
            if(begin==string::npos){//Not enough words, return everything
                return s.substr(0,delim+1);
            }
        }
        return s.substr(begin+1,delim-begin);
    }
    inline string Next_SubMessage(const string &prev){//Generate next part of message from the N_Markov words before it
        uniform_int_distribution<long> Word_Distrib(0,words[prev].total_weights);
        long word{Word_Distrib(generator)};
        for(auto W:words[prev].next){
            word-=W.second;
            if(word<=0){
                return W.first;
            }
        }
        cerr << "Error: Reached end of Next_SubMessage() function" << endl;
        throw(0);
    }
    inline string Generate_Sentence(const string &start){//Generate sentence with a markov chain model
        string sentence=start;
        int n_words{Words(start)};
        cerr << "Chain length: ";
        while(++n_words<25){
            //Adaptative markov chain length
            int N{0};
            while(++N>0){
                string prev{Last_Words(sentence,N,-1)};
                //cerr << "Total weights of  " << prev << " : " <<  words[prev].total_weights << endl;
                if(words[prev].total_weights<Occurence_Limit){//This should never happen given that useless content is removed at learning time now
                    --N;
                    break;
                }
                else if(prev==sentence.substr(0,sentence.find_last_not_of(' ')+1)){
                    break;
                }
            }
            N=max(N,1);
            cerr << N << " ";
            string next{Next_SubMessage(Last_Words(sentence,N,-1))};
            //cerr << N << " " << next << endl;
            if(next==End_Str){
                break;
            }
            sentence+=next+" ";
        }
        cerr << endl;
        cerr << "Generated: " << sentence << endl;
        return sentence;
    }
    inline string talk(){
        return Generate_Sentence("");
    }
    inline void Reinforce(const string &a,const string &b){//Reinforce the connection from a->b
        //cerr << "Reinforce " << a << " -> " << b << endl;
        ++words[a].next[b];
        ++words[a].total_weights;
    }
    inline string Remove_All_Words(const string &s,const string &sub)const{//Remove all words containing the substring sub
        return regex_replace(s,regex(sub,regex_constants::icase),"");
    }
    inline string Filter_Message(const string &mess)const{
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
            string next{Next_Word(mess,delim)};
            string prev_max{Last_Words(mess,N_Markov,delim)};
            int N{0};
            while(++N>0){
                string prev{Last_Words(mess,N,delim)};
                //cerr << "Delim: " << delim << " " << prev << " -> " << next << endl;
                Reinforce(prev,next);
                if(prev==prev_max){
                    break;
                }
            }
            delim=mess.find_first_not_of(' ',mess.find_first_of(' ',delim));
        }
        string prev_max{Last_Words(mess,N_Markov,delim)};
        int N{0};
        while(++N>0){
            string prev{Last_Words(mess,N,delim)};
            //cerr << "Delim: " << delim << " " << prev << " -> " << next << endl;
            Reinforce(prev,End_Str);
            if(prev==prev_max){
                break;
            }
        }
    }
    inline void Learn_From_Message(const Message &msg){
        string mess{msg.body()};
        mess.erase(0,mess.find(" : ")+3);//Get rid of "(HH/MM/SS) Username : "
        Learn_From_Message(mess);
    }
    inline void Log(const Message &msg)const{
        time_t t = time(nullptr);
        tm ptm = *localtime(&t);
        stringstream ss;
        ss << "./Logs/"+room_name+"@"+MUC+"-" << put_time(&ptm,"%F") << ".log";
        ofstream log_file(ss.str().c_str(),ios::app);
        string message_body{msg.body()};
        replace(message_body.begin(),message_body.end(),'\n',' ');
        log_file << "(" << put_time(&ptm,"%T") << ") " << msg.from().resource() <<  " : " << message_body << endl;
    }
    inline void LearnFromLogFile(const string &logfilename){
        ifstream logfile(logfilename);
        if(!logfile){
            cerr << "Couldn't open log file: " << room_name+".log" << endl;
        }
        while(logfile){
            string line,username;
            getline(logfile,line);
            stringstream ss(line);
            ss >> username >> username;
            if(Ignored_Talkers.find(username)==Ignored_Talkers.end()){
                line.erase(0,line.find(" : ")+3);//Get rid of "(HH/MM/SS) Username : "
                Learn_From_Message(line);
            }
        }
    }
    inline void Filter_Markov_Chain(){
    	//Filter out words from content which is too rare to be used by the variable length Markov Chain
        //3167708ko->1602424ko
        bool to_clear{true};
        while(to_clear){
        	to_clear=false;
        	for(auto it=words.begin();it!=words.end();){
	        	if((it->second).total_weights<Occurence_Limit){
	        		it=words.erase(it);
	        	}
	        	else{
	        		++it;
	        	}
	        }
	        for(auto &w_pair:words){
	        	next_words &n{w_pair.second};
	        	for(auto it{n.next.begin()};it!=n.next.end();){
	        		if(words.find(it->first)==words.end()){//Remove word successors which have been eliminated in the previous pass
	        			n.total_weights-=it->second;
	        			if(n.total_weights<Occurence_Limit){
	        				to_clear=true;
	        			}
	        			it=n.next.erase(it);
	        		}
	        		else{
	        			++it;
	        		}
	        	}
	        	//map<string,int> temp(n.next);
	        	//n.next.swap(temp);
	        }
        }
        words.rehash(0);//1602424ko->1595868ko
    }
    inline void LearnFromLogs(){
        if(fs::exists("Logs")){
            for(const fs::directory_entry& entry:fs::directory_iterator("Logs")){
                const string filename{entry.path().filename()};
                if(filename.substr(0,filename.find_first_of('@'))==room_name){
                    LearnFromLogFile("./Logs/"+filename);
                }
            }
            Filter_Markov_Chain();
        }
        else{
            cerr << "Could not Logs directory" << endl;
        }
    }
};

struct Bot : public MessageHandler,ConnectionListener,MUCRoomHandler,MessageSessionHandler{
    Client* client;
    vector<ChannelBot> Channel;
    vector<MessageSession*> MsgSession;
    set<string> Ignored_Talkers;
    int codingame_id,port;
    string password,host,MUC,nickname;
    Bot(){
        Read_Config_File();
        stringstream ss_client_jid;
        ss_client_jid << codingame_id << "@" << host;
        JID jid(ss_client_jid.str());
        client=new Client(jid,password,port);
        //client->registerMessageHandler(this);
        client->registerMessageSessionHandler(this,0);
        client->registerConnectionListener(this);
        client->setPresence(Presence::Available,-1);
        for(ChannelBot &C:Channel){
            C.room=new MUCRoom(client,JID(C.room_name+"@"+MUC+"/"+C.nickname),this,0);
        }
        client->connect(true);
    }
    ~Bot(){
        delete client;
        for(auto msg_sess:MsgSession){
            delete msg_sess;
        }
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
            Ignored_Talkers.insert(nickname);
            if(talker_name.find_first_not_of(' ')!=string::npos){
                Ignored_Talkers.insert(talker_name);
            }
        }
        getline(config,line);
        stringstream ss_rooms(line);
        while(ss_rooms){
            string room_name;
            ss_rooms >> room_name;
            if(room_name.find_first_not_of(' ')!=string::npos){
                Channel.push_back(ChannelBot(nickname,room_name,MUC,JID(room_name+"@"+MUC),Ignored_Talkers));
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
                        if(Ignored_Talkers.find(msg.from().resource())==Ignored_Talkers.end()){
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
    virtual void handleMessageSession(MessageSession* session){
        cerr << "New message session" << endl;
        MsgSession.push_back(session);
        session->registerMessageHandler(this);
    }
    virtual void onDisconnect(ConnectionError e) {
        cerr << "ConnListener::onDisconnect() " << e << endl;
    }
    virtual bool onTLSConnect(const CertInfo& info){
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