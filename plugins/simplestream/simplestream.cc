#include "../../trunk-recorder/plugin_manager/plugin_api.h"
#include "../../trunk-recorder/recorders/recorder.h"
#include <boost/dll/alias.hpp> // for BOOST_DLL_ALIAS
#include <boost/foreach.hpp>
#include <boost/asio.hpp>
#include <boost/thread/mutex.hpp>
#include <map>

using namespace boost::asio;

typedef struct plugin_t plugin_t;
typedef struct stream_t stream_t;
std::map<unsigned long,std::vector<unsigned long>> TGID_map;
std::vector<stream_t> streams;
boost::mutex TGID_map_mutex;

struct plugin_t {
  Config* config;
};

struct stream_t {
  unsigned long TGID;
  std::string address;
  long port;
  ip::udp::endpoint remote_endpoint;
  bool sendTGID = false;
};


class Simple_Stream : public Plugin_Api {
  typedef boost::asio::io_service io_service;
  
  io_service my_io_service;
  ip::udp::endpoint remote_endpoint;
  ip::udp::socket my_socket{my_io_service};
  public:
  
  Simple_Stream(){
      
  }
    
  int call_start(Call *call) {
    boost::mutex::scoped_lock lock(TGID_map_mutex);
    BOOST_LOG_TRIVIAL(debug) << "call_start called in simplestream plugin" ;
    unsigned long talkgroup_num = call->get_talkgroup();
    std::vector<unsigned long> patched_talkgroups = call->get_system()->get_talkgroup_patch(talkgroup_num);
    BOOST_LOG_TRIVIAL(debug) << "call_start called in simplestream plugin for TGID "<< talkgroup_num << " with patch size " << patched_talkgroups.size();
    if (patched_talkgroups.size() == 0){
      patched_talkgroups.push_back(talkgroup_num);
    }
    BOOST_LOG_TRIVIAL(debug) << "TGID is "<<talkgroup_num ;
    Recorder *recorder = call->get_recorder();
    if (recorder != NULL) {
      int recorder_id = recorder->get_num();
      BOOST_LOG_TRIVIAL(debug) << "Recorder num is "<<recorder_id ;
      TGID_map[recorder_id] = patched_talkgroups;
    }
    else {
      BOOST_LOG_TRIVIAL(debug) << "No Recorder for this TGID...doing nothing! ";
    }
    return 0;
  }
    
  int call_end(Call_Data_t call_info) {
    boost::mutex::scoped_lock lock(TGID_map_mutex);
    unsigned long talkgroup_num = call_info.talkgroup;
    std::vector<unsigned long> patched_talkgroups = call_info.patched_talkgroups;
    std::vector<long> recorders_to_erase;
    BOOST_LOG_TRIVIAL(debug) << "call_end called in simplestream plugin on TGID " << talkgroup_num << " with patch size " << patched_talkgroups.size() ;
    BOOST_FOREACH(auto& element, TGID_map){
      BOOST_FOREACH(unsigned long mapped_TGID, element.second){
        BOOST_LOG_TRIVIAL(debug) << "TGID_map[" << element.first << "] contains " << mapped_TGID;
        if (mapped_TGID == talkgroup_num){
          recorders_to_erase.push_back(element.first);
          BOOST_LOG_TRIVIAL(debug) << "adding recorder " << element.first << " to erase list";
        }
        BOOST_FOREACH(unsigned long TGID, patched_talkgroups){
          if (mapped_TGID == TGID){
            recorders_to_erase.push_back(element.first);
            BOOST_LOG_TRIVIAL(debug) << "adding recorder " << element.first << " to erase list";
          }
        }
      }
    }
    BOOST_FOREACH(long recorder_id, recorders_to_erase){
      BOOST_LOG_TRIVIAL(debug) << "erasing recorder " << recorder_id << " from TGID_Map";
      TGID_map.erase(recorder_id);
    }
    return 0;
  }

  int parse_config(boost::property_tree::ptree &cfg) {
    BOOST_FOREACH (boost::property_tree::ptree::value_type &node, cfg.get_child("streams")) {
      stream_t stream;
      stream.TGID = node.second.get<unsigned long>("TGID");
      stream.address = node.second.get<std::string>("address");
      stream.port = node.second.get<long>("port");
      stream.remote_endpoint = ip::udp::endpoint(ip::address::from_string(stream.address), stream.port);
      stream.sendTGID = node.second.get<bool>("sendTGID");
      BOOST_LOG_TRIVIAL(info) << "simplestreamer will stream audio from TGID " <<stream.TGID << " to " << stream.address <<" on port " << stream.port;
      streams.push_back(stream);
    }
    return 0;
  }
  
  int audio_stream(Recorder *recorder, int16_t *samples, int sampleCount){
    int recorder_id = recorder->get_num();
    BOOST_FOREACH (auto& stream, streams){
      if (TGID_map.find(recorder_id) != TGID_map.end()){
        BOOST_FOREACH (auto& TGID, TGID_map[recorder_id]){
          if (TGID==stream.TGID || stream.TGID==0){  //setting TGID to 0 in the config file will stream everything
            boost::system::error_code err;
            BOOST_LOG_TRIVIAL(debug) << "got " <<sampleCount <<" samples - " <<sampleCount*2<<" bytes";
            if (stream.sendTGID==true){
              //prepend 4 byte long tgid to the audio data
              boost::array<mutable_buffer, 2> buf1 = {
                buffer(&TGID,4),
                buffer(samples, sampleCount*2)
              };
              my_socket.send_to(buf1, stream.remote_endpoint, 0, err);
            }
            else{
              //just send the audio data
              my_socket.send_to(buffer(samples, sampleCount*2), stream.remote_endpoint, 0, err);
            }
          }
        }
      }
    }
    return 0;
  }
  
  int start(){
	my_socket.open(ip::udp::v4());
	return 0;
  }
  
  int stop(){
	  my_socket.close();
	  return 0;
  }

  static boost::shared_ptr<Simple_Stream> create() {
    return boost::shared_ptr<Simple_Stream>(
        new Simple_Stream());
  }
};

BOOST_DLL_ALIAS(
    Simple_Stream::create, // <-- this function is exported with...
    create_plugin             // <-- ...this alias name
)
