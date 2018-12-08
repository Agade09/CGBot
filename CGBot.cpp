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
using namespace std::chrono;

constexpr char Start_Str[]{""},End_Str[]{"\0"};
constexpr int N_Markov{4};//Markov chain length
constexpr int Occurence_Limit{2};//Minimum occurences to accept a certain chain length when speaking

default_random_engine generator{static_cast<unsigned int>(system_clock::now().time_since_epoch().count())};

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
    unordered_map<string,next_words> words;
    set<string> Ignored_Talkers;
    ChannelBot(const string &nick,const string &RName,const set<string> &ITalkers):nickname{nick},room_name{RName},Ignored_Talkers{ITalkers}{
        time_point<system_clock> Start_Time{system_clock::now()};
        LearnFromLogs();
        cerr << nickname << " took " << static_cast<duration<double>>(system_clock::now()-Start_Time).count() << "s to process the logs of room " << room_name << endl;
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
        cerr << "Error: Reached end of Next_SubMessage()" << endl;
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
                if(words[prev].total_weights<Occurence_Limit){
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
        mess.erase(0,mess.find(" : ")+3);//Get rid of "Username : "
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
    inline void LearnFromLogFile(const string &logfilename){
        ifstream logfile(logfilename);
        if(!logfile){
            cerr << "Couldn't open log file: " << logfilename << endl;
        }
        while(logfile){
            string line,username;
            getline(logfile,line);
            stringstream ss(line);
            ss >> username >> username;
            if(Ignored_Talkers.find(username)==Ignored_Talkers.end()){
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
                if(filename.size()>room_name.size() && equal(room_name.begin(),room_name.end(),filename.begin())){
                    LearnFromLogFile("./Logs/"+filename);
                }
            }
            Filter_Markov_Chain();
        }
        else{
            cerr << "Could not Logs directory" << endl;
        }
    }
    inline void Log(string msg)const{
        time_t t = time(nullptr);
        tm ptm = *localtime(&t);
        stringstream ss;
        ss << "./Logs/"+room_name+"-" << put_time(&ptm,"%F") << ".log";
        ofstream log_file(ss.str().c_str(),ios::app);
        replace(msg.begin(),msg.end(),'\n',' ');
        log_file << "(" << put_time(&ptm,"%T") << ") " << msg << endl;
    }
};

int main(int argc,char **argv){
    if(argc<3){
        cerr << "Program takes at least two arguments: the name of the chat room and the name of the bot" << endl;
    }
    string chat_room=argv[1],nickname=argv[2];
    set<string> Ignored_Talkers;
    Ignored_Talkers.insert(nickname);
    for(int i=3;i<argc;++i){
        Ignored_Talkers.insert(string(argv[i]));
    }
    ChannelBot b(nickname,chat_room,Ignored_Talkers);
    while(true){
        string message_body_raw;
        stringstream ss(message_body_raw);
        string username;
        ss >> username >> username;
        getline(cin,message_body_raw);
        b.Log(message_body_raw);
        if(Ignored_Talkers.find(username)==Ignored_Talkers.end()){
            b.Learn_From_Message(message_body_raw);
            if(regex_search(message_body_raw.begin(),message_body_raw.end(),regex(nickname,regex_constants::icase))){
                cout << b.talk() << endl;
            }
        }
    }
}