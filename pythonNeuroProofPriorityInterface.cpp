#include "pythonNeuroProofPriorityInterface.h"
#include "Utilities/ErrMsg.h"
#include "Priority/LocalEdgePriority.h"
#include "ImportsExports/ImportExportRagPriority.h"
#include <json/json.h>
#include <json/value.h>

#include <fstream>
#include <boost/python.hpp>
//#include <Python.h>

using namespace NeuroProof;
using namespace boost::python;
using std::ifstream;

static LocalEdgePriority<Label>* priority_scheduler = 0;
Rag<Label>* rag = 0;

// false if file does not exist or json is not properly formatted
// exception thrown if min, max, or start val are illegal
bool initialize_priority_scheduler(const char * json_file, double min_val, double max_val, double start_val)
{
    if ((min_val < 0) || (max_val > 1.0) || (min_val > max_val) || (start_val > max_val) || (start_val < min_val) ) {
        throw ErrMsg("Priority scheduler filter bounds not properly set");  
    }
    if (!priority_scheduler) {
        delete priority_scheduler;
    }
    
    rag = create_rag_from_jsonfile(json_file);
    if (!rag) {
        return false;
    }

    ifstream fin(json_file);
    Json::Reader json_reader;
    Json::Value json_vals;
    if (!json_reader.parse(fin, json_vals)) {
        throw ErrMsg("Error: Json incorrectly formatted");
    }
    fin.close();

    Json::Value json_range = json_vals["range"];
    if (!json_range.empty()) {
        min_val = json_range[(unsigned int)(0)].asDouble();
        max_val = json_range[(unsigned int)(1)].asDouble();
        start_val = min_val;
    }


    priority_scheduler = new LocalEdgePriority<Label>(*rag, min_val, max_val, start_val);
    priority_scheduler->updatePriority();

    return true;
}

// false if file cannot be written
bool export_priority_scheduler(const char * json_file)
{
    if (!priority_scheduler) {
        throw ErrMsg("Scheduler not initialized");
    }
    
    return create_jsonfile_from_rag(rag, json_file);
}




// empty PriorityInfo if no more edges
PriorityInfo get_next_edge()
{
    PriorityInfo priority_info;
    if (!priority_scheduler) {
        throw ErrMsg("Scheduler not initialized");
    }

    if (priority_scheduler->isFinished()) {
        return priority_info;
    }
    boost::tuple<unsigned int, unsigned int, unsigned int> location;
    boost::tuple<Label, Label> body_pair = priority_scheduler->getTopEdge(location);
   
    priority_info.body_pair = make_tuple(boost::get<0>(body_pair), boost::get<1>(body_pair));
    priority_info.location = make_tuple(boost::get<0>(location), boost::get<1>(location), boost::get<2>(location));
    return priority_info;
}


// exception throw if edge does not exist or connection probability specified is illegal
void set_edge_result(tuple body_pair, bool remove)
{
    if (!priority_scheduler) {
        throw ErrMsg("Scheduler not initialized");
    }

    Label l1 = extract<Label>(body_pair[0]);
    Label l2 = extract<Label>(body_pair[1]);
    boost::tuple<Label, Label> body_pair_temp(l1, l2);
    priority_scheduler->removeEdge(body_pair_temp, remove);
}


// number of edges yet to be processed in the scheduler
unsigned int get_estimated_num_remaining_edges()
{
    if (!priority_scheduler) {
        throw ErrMsg("Scheduler not initialized");
    }

    return priority_scheduler->getNumRemaining();
}

double get_average_prediction_error()
{
    if (!priority_scheduler) {
        throw ErrMsg("Scheduler not initialized");
    }
    return priority_scheduler->getAveragePredictionError();
}

double get_percent_prediction_correct()
{
    if (!priority_scheduler) {
        throw ErrMsg("Scheduler not initialized");
    }
    return priority_scheduler->getPercentPredictionCorrect();
}

bool undo()
{
    if (!priority_scheduler) {
        throw ErrMsg("Scheduler not initialized");
    }

    return priority_scheduler->undo();
}


BOOST_PYTHON_MODULE(libNeuroProofPriority)
{
    def("initialize_priority_scheduler", initialize_priority_scheduler);
    def("export_priority_scheduler", export_priority_scheduler);
    def("get_next_edge", get_next_edge);
    def("set_edge_result", set_edge_result);
    def("undo", undo);
    def("get_percent_prediction_correct", get_percent_prediction_correct);
    def("get_average_prediction_error", get_average_prediction_error);
    def("get_estimated_num_remaining_edges", get_estimated_num_remaining_edges);

    class_<PriorityInfo>("PriorityInfo")
        .def_readwrite("body_pair", &PriorityInfo::body_pair)
        .def_readonly("location", &PriorityInfo::location);
}

