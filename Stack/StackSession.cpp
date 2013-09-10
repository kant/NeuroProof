#include "StackSession.h"
#include "../Rag/RagUtils.h"
#include "Stack.h"
#include "../Rag/RagIO.h"

#include <boost/algorithm/string/predicate.hpp>
#include <boost/filesystem.hpp>

using namespace NeuroProof;
using std::tr1::unordered_set;
using namespace boost::algorithm;
using namespace boost::filesystem;
using std::string;
using std::vector;

// to be called from command line
StackSession::StackSession(string session_name)
{
    initialize();
  
    if (ends_with(session_name, ".h5")) {
        stack = new Stack(session_name);
        stack->build_rag();
    } else {
        // load label volume to examine
        stack = new Stack(session_name + "/stack.h5"); 
        string rag_name = session_name + "/graph.json";
        Rag_t* rag = create_rag_from_jsonfile(rag_name.c_str());
        stack->set_rag(RagPtr(rag));
        
        // load gt volume to examine
        string gt_name = session_name + "/gtstack.h5";
        if (exists(gt_name.c_str())) {
            load_gt(gt_name, false); 
            string gtrag_name = session_name + "/gtgraph.json";
            Rag_t* gtrag = create_rag_from_jsonfile(gtrag_name.c_str());
            gt_stack->set_rag(RagPtr(gtrag));
        }
      
        // record session name for saving  
        saved_session_name = session_name;
    }

    if (!(stack->get_labelvol())) {
        throw ErrMsg("Label volume not defined for stack");
    }
    if (!(stack->get_grayvol())) {
        throw ErrMsg("Gray volume not defined for stack");
    }
}

// ?! check same dimensions
void StackSession::load_gt(string gt_name, bool build_rag)
{
    Stack* new_gt_stack;
    try {
        new_gt_stack = new Stack(gt_name);
        
        if (new_gt_stack->get_xsize() != stack->get_xsize() ||
            new_gt_stack->get_ysize() != stack->get_ysize() ||
            new_gt_stack->get_zsize() != stack->get_zsize()) {
            throw ErrMsg("Dimensions of ground truth do not match label volume");
        }
        if (build_rag) {
            new_gt_stack->build_rag();
        }
        new_gt_stack->set_grayvol(stack->get_grayvol());
    } catch (std::runtime_error &error) {
        throw ErrMsg(gt_name + string(" not loaded"));
    }

    if (gt_stack) {
        delete gt_stack;
    }
    gt_stack = new_gt_stack;
}

// to be called from gui controller
StackSession::StackSession(vector<string>& gray_images, string labelvolume)
{
    initialize();
    try {
        VolumeLabelPtr initial_labels = VolumeLabelData::create_volume(
                labelvolume.c_str(), "stack");
        stack = new Stack(initial_labels);

        VolumeGrayPtr gray_data = VolumeGray::create_volume_from_images(gray_images);
        stack->set_grayvol(gray_data);
        stack->build_rag();
    } catch (std::runtime_error &error) {
        throw ErrMsg("Unspecified load error");
    }
}

void StackSession::save()
{
    if (saved_session_name == "") {
        throw ErrMsg("Session has not been created");
    }
    export_session(saved_session_name);
}

void StackSession::export_session(string session_name)
{
    // flip stacks just for export if gt toggled
    Stack* stack_exp = stack;
    Stack* gtstack_exp = gt_stack;
    if (gt_mode) {
        stack_exp = gt_stack;
        gtstack_exp = stack; 
    }    

    if (stack_exp) {
        try {
            path dir(session_name.c_str()); 
            create_directories(dir);

            string stack_name = session_name + "/stack.h5"; 
            string graph_name = session_name + "/graph.json"; 
            stack_exp->serialize_stack(stack_name.c_str(), graph_name.c_str(), false);
            
            if (gtstack_exp) {
                // do not export grayscale from ground truth -- already exported
                gtstack_exp->set_grayvol(VolumeGrayPtr());
                string gtstack_name = session_name + "/gtstack.h5"; 
                string gtgraph_name = session_name + "/gtgraph.json"; 
                gtstack_exp->serialize_stack(gtstack_name.c_str(), gtgraph_name.c_str(), false);
                gtstack_exp->set_grayvol(stack->get_grayvol());
            }
        } catch (...) {
            throw ErrMsg(session_name.c_str());
        }
        saved_session_name = session_name;
    }
}

bool StackSession::has_gt_stack()
{
    return gt_stack != 0;
} 

bool StackSession::has_session_name()
{
    return saved_session_name != "";
}

string StackSession::get_session_name()
{
    return saved_session_name;
}

void StackSession::compute_label_colors(RagPtr rag)
{
    compute_graph_coloring(rag);
}

void StackSession::increment_plane()
{
    if (active_plane < (stack->get_grayvol()->shape(2)-1)) {
        set_plane(active_plane+1);
    }
}

void StackSession::initialize()
{
    stack = 0;
    gt_stack = 0;
    active_labels_changed = false;
    selected_id = 0;
    old_selected_id = 0;
    selected_id_changed = false;
    show_all = true;
    show_all_changed = false;
    active_plane = 0;
    active_plane_changed = false;
    opacity = 3;
    opacity_changed = false;
    saved_session_name = string("");
    gt_mode = false;
    toggle_gt_changed = false;
}

void StackSession::decrement_plane()
{
    if (active_plane > 0) {
        set_plane(active_plane-1);
    }
}

void StackSession::toggle_gt()
{
    if (!gt_stack) {
        throw ErrMsg("GT stack not defined");
    }

    reset_active_labels();
    gt_mode = !gt_mode;
    Stack* temp_stack;
    temp_stack = stack;
    stack = gt_stack;
    gt_stack = temp_stack;
    toggle_gt_changed = true;
    update_all();
    toggle_gt_changed = false;
}

bool StackSession::get_curr_labels(VolumeLabelPtr& labelvol, RagPtr& rag)
{
    labelvol = stack->get_labelvol();
    rag = stack->get_rag();

    return toggle_gt_changed;
}

bool StackSession::is_gt_mode()
{
    return gt_mode;
}

void StackSession::set_opacity(unsigned int opacity_)
{   
    opacity = opacity_; 
    opacity_changed = true;
    update_all();
    opacity_changed = false;
}

void StackSession::set_plane(unsigned int plane)
{   
    active_plane = plane; 
    active_plane_changed = true;
    update_all();
    active_plane_changed = false;
}

void StackSession::toggle_show_all()
{
    show_all = !show_all;
    show_all_changed = true;
    update_all();
    show_all_changed = false;
}

bool StackSession::get_plane(unsigned int& plane_id)
{
    plane_id = active_plane;
    return active_plane_changed;
}

bool StackSession::get_opacity(unsigned int& opacity_)
{
    opacity_ = opacity;
    return opacity_changed;
}
bool StackSession::get_show_all(bool& show_all_)
{
    show_all_ = show_all;
    return show_all_changed;
}

bool StackSession::get_select_label(Label_t& select_curr, Label_t& select_old)
{
    select_curr = selected_id;
    select_old = old_selected_id;
    return selected_id_changed;
}

void StackSession::get_rgb(int color_id, unsigned char& r,
        unsigned char& g, unsigned char& b)
{
    unsigned int val = color_id % 18; 
    switch (val) {
        case 0:
            r = 0xff; g = b = 0; break;
        case 1:
            g = 0xff; r = b = 0; break;
        case 2:
            b = 0xff; r = g = 0; break;
        case 3:
            r = g = 0xff; b = 0; break;
        case 14:
            r = b = 0xff; g = 0; break;
        case 8:
            g = b = 0xff; r = 0; break;
        case 6:
            r = 0x7f; g = b = 0; break;
        case 16:
            g = 0x7f; r = b = 0; break;
        case 9:
            b = 0x7f; r = g = 0; break;
        case 5:
            r = g = 0x7f; b = 0; break;
        case 13:
            r = b = 0x7f; g = 0; break;
        case 11:
            g = b = 0x7f; r = 0; break;
        case 12:
            r = 0xff; g = b = 0x7f; break;
        case 10:
            g = 0xff; r = b = 0x7f; break;
        case 17:
            b = 0xff; r = g = 0x7f; break;
        case 15:
            r = g = 0xff; b = 0x7f; break;
        case 7:
            r = b = 0xff; g = 0x7f; break;
        case 4:
            g = b = 0xff; r = 0x7f; break;
    }
}



void StackSession::active_label(unsigned int x, unsigned int y, unsigned int z)
{
    VolumeLabelPtr labelvol = stack->get_labelvol();
    Label_t current_label = (*labelvol)(x,y,z);
    
    if (!current_label) {
        // ignore selection if off image or on boundary
        return;
    }
   
    if (active_labels.find(current_label) != active_labels.end()) {
        active_labels.erase(current_label);        
    } else {
        active_labels.insert(current_label);
    }
    
    select_label(selected_id);

    active_labels_changed = true;
    update_all();
    active_labels_changed = false;
}



void StackSession::select_label(unsigned int x, unsigned int y, unsigned int z)
{
    VolumeLabelPtr labelvol = stack->get_labelvol();
    Label_t current_label = (*labelvol)(x,y,z);

    select_label(current_label);    
}

void StackSession::select_label(Label_t current_label)
{
    if (!current_label) {
        // ignore selection if off image or on boundary
        return;
    }
    if (!active_labels.empty() &&
            (active_labels.find(current_label) == active_labels.end())) {
        return;
    }
    
    old_selected_id = selected_id;
    if (current_label != selected_id) {
        selected_id = current_label;
    } else {
        selected_id = 0;
    }
    selected_id_changed = true;
    update_all();
    selected_id_changed = false;
}

bool StackSession::get_active_labels(unordered_set<Label_t>& active_labels_)
{
    active_labels_ = active_labels;
    return active_labels_changed;
}

void StackSession::reset_active_labels()
{
    active_labels.clear();
    active_labels_changed = true;
    show_all = true;
    show_all_changed = true;
    update_all();
    active_labels_changed = false;
    show_all_changed = false;
}


