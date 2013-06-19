#include "BioStackController.h"
#include "StackLearnAlgs.h"
#include "../Algorithms/FeatureJoinAlgs.h"
#include "../Algorithms/MergePriorityQueue.h"

namespace NeuroProof {

void preprocess_stack(BioStackController& controller, bool use_mito)
{
    cout << "Building RAG ..."; 	
    if (use_mito) {
        controller.build_rag_mito();
    } else {
        controller.build_rag();
    }
    
    cout << "done with " << controller.get_num_labels() << " nodes" << endl;


    cout << "Inclusion removal ..."; 
    controller.remove_inclusions();
    cout << "done with " << controller.get_num_labels() << " nodes" << endl;

    cout << "gt label counting" << endl;

    controller.compute_groundtruth_assignment(); 	
}


void learn_edge_classifier_flat(BioStackController& controller, double threshold,
        UniqueRowFeature_Label& all_featuresu, vector<int>& all_labels, bool use_mito)
{
    preprocess_stack(controller, use_mito);

    Stack2* stack = controller.get_stack();
    RagPtr rag = stack->get_rag();
    FeatureMgrPtr feature_mgr = stack->get_feature_manager();

    for (Rag_uit::edges_iterator iter = rag->edges_begin(); iter != rag->edges_end(); ++iter) {
        if ( (!(*iter)->is_preserve()) && (!(*iter)->is_false_edge()) ) {
	    RagEdge_uit* rag_edge = *iter; 	

            RagNode_uit* rag_node1 = rag_edge->get_node1();
            RagNode_uit* rag_node2 = rag_edge->get_node2();
            Label label1 = rag_node1->get_node_id(); 
            Label label2 = rag_node2->get_node_id(); 

	    int edge_label = controller.find_edge_label(label1, label2);
            if (use_mito && (controller.is_mito(label1) || 
                             controller.is_mito(label2))) {
                edge_label = 1; 
            }

            if ( edge_label ){	
		vector<double> feature;
		feature_mgr->compute_all_features(rag_edge, feature);

		feature.push_back(edge_label);
		all_featuresu.insert(feature);
	    }	
        }
    }

    vector<vector<double> > all_features;
    all_featuresu.get_feature_label(all_features, all_labels); 	    	

    cout << "Features generated" << endl;
    feature_mgr->get_classifier()->learn(all_features, all_labels); // number of trees
    cout << "Classifier learned" << endl;

    /*debug*/
    double err=0;
    for(int fcount=0;fcount<all_features.size(); fcount++){
	double predp = feature_mgr->get_classifier()->predict(all_features[fcount]);
	int predl = (predp>0.5)? 1:-1;	
	err+= ((predl==all_labels[fcount])?0:1);	
    }
    cout << "accuracy = " << 100*(1 - err/all_labels.size()) << endl;
}

void learn_edge_classifier_queue(BioStackController& controller, double threshold,
        UniqueRowFeature_Label& all_featuresu, vector<int>& all_labels,
        bool accumulate_all, bool use_mito)
{
    preprocess_stack(controller, use_mito);
    
    Stack2* stack = controller.get_stack();
    RagPtr rag = stack->get_rag();
    FeatureMgrPtr feature_mgr = stack->get_feature_manager();
   
    vector<QE> all_edges;	    	
    int count = 0; 	
    for (Rag_uit::edges_iterator iter = rag->edges_begin(); iter != rag->edges_end(); ++iter) {
        if ( (!(*iter)->is_preserve()) && (!(*iter)->is_false_edge()) ) {
            double val = feature_mgr->get_prob(*iter);
            (*iter)->set_weight(val);
	    (*iter)->set_property("qloc", count);
	    Label node1 = (*iter)->get_node1()->get_node_id();	
	    Label node2 = (*iter)->get_node2()->get_node_id();	

            QE tmpelem(val, make_pair(node1, node2));	
	    all_edges.push_back(tmpelem); 
	
  	    count++;
        }
    }

    MergePriorityQueue<QE> *Q = new MergePriorityQueue<QE>(rag.get());
    Q->set_storage(&all_edges);	

    clock_t start = clock();
    PriorityQCombine node_combine_alg(feature_mgr.get(), rag.get(), Q); 

    while (!Q->is_empty()){
	QE tmpqe = Q->heap_extract_min();	

	Label node1 = tmpqe.get_val().first;
	Label node2 = tmpqe.get_val().second;
        RagEdge_uit* rag_edge = rag->find_rag_edge(node1, node2);


        if (!rag_edge || !tmpqe.valid()) {
            continue;
        }
        RagNode_uit* rag_node1 = rag_edge->get_node1();
        RagNode_uit* rag_node2 = rag_edge->get_node2();

        Label_t label1 = rag_node1->get_node_id(); 
        Label_t label2 = rag_node2->get_node_id(); 

        int edge_label = controller.find_edge_label(label1, label2);
        if (use_mito && (controller.is_mito(label1) || 
                    controller.is_mito(label2))) {
            edge_label = 1; 
        }

	if  (edge_label){  
	    vector<double> feature;
	    feature_mgr->compute_all_features(rag_edge,feature);

	    if(accumulate_all){ 	

		feature.push_back(edge_label);
	        all_featuresu.insert(feature);
	    }	
 	    else if (feature_mgr->get_classifier()->is_trained()){ 		
	        double predp = feature_mgr->get_classifier()->predict(feature);		 
	        int predl = (predp > threshold)? 1:-1;	
		if (predl!=edge_label){

		    feature.push_back(edge_label);
	            all_featuresu.insert(feature);
		}
	    }
	}	

	if ( edge_label == -1 ){ //merge
	    controller.merge_labels(label2, label1, &node_combine_alg);
	}
    }

    vector<vector<double> > all_features;
    all_featuresu.get_feature_label(all_features, all_labels);	

    cout << "Features generated in " << (((double)clock() - start) / CLOCKS_PER_SEC) 
        << " secs" << endl;
    feature_mgr->get_classifier()->learn(all_features, all_labels); // number of trees
    cout << "Classifier learned" << endl;

    // debug
    double err=0;
    for(int fcount=0;fcount<all_features.size(); fcount++){
	double predp = feature_mgr->get_classifier()->predict(all_features[fcount]);
	int predl = (predp>0.5)? 1:-1;	
	err += ((predl==all_labels[fcount])?0:1);	
    }
    cout << "accuracy = " << 100*(1 - err/all_labels.size()) << endl;	
}

void learn_edge_classifier_lash(BioStackController& controller, double threshold,
        UniqueRowFeature_Label& all_featuresu, vector<int>& all_labels, bool use_mito)
{
    preprocess_stack(controller, use_mito);
    
    Stack2* stack = controller.get_stack();
    RagPtr rag = stack->get_rag();
    FeatureMgrPtr feature_mgr = stack->get_feature_manager();

    all_featuresu.clear();
    all_labels.clear();	
    vector<QE> all_edges;	    	

    int count=0; 	
    for (Rag_uit::edges_iterator iter = rag->edges_begin(); iter != rag->edges_end(); ++iter) {
        if ( (!(*iter)->is_preserve()) && (!(*iter)->is_false_edge()) ) {
            double val = feature_mgr->get_prob(*iter);
            (*iter)->set_weight(val);
	    (*iter)->set_property("qloc", count);

	    Label node1 = (*iter)->get_node1()->get_node_id();	
	    Label node2 = (*iter)->get_node2()->get_node_id();	

            QE tmpelem(val, make_pair(node1, node2));	
	    all_edges.push_back(tmpelem); 
	
  	    count++;
        }
    }

    MergePriorityQueue<QE> *Q = new MergePriorityQueue<QE>(rag.get());
    Q->set_storage(&all_edges);	
    PriorityQCombine node_combine_alg(feature_mgr.get(), rag.get(), Q); 

    clock_t start = clock();
    while (!Q->is_empty()){
	QE tmpqe = Q->heap_extract_min();	

	Label node1 = tmpqe.get_val().first;
	Label node2 = tmpqe.get_val().second;
        RagEdge_uit* rag_edge = rag->find_rag_edge(node1, node2);


        if (!rag_edge || !tmpqe.valid()) {
            continue;
        }


        RagNode_uit* rag_node1 = rag_edge->get_node1();
        RagNode_uit* rag_node2 = rag_edge->get_node2();

        Label_t label1 = rag_node1->get_node_id(); 
        Label_t label2 = rag_node2->get_node_id(); 

        int edge_label = controller.find_edge_label(label1, label2);
        if (use_mito && (controller.is_mito(label1) || 
                    controller.is_mito(label2))) {
            edge_label = 1; 
        }





	if  (edge_label){  
	    vector<double> feature;
	    feature_mgr->compute_all_features(rag_edge,feature);


	    feature.push_back(edge_label);	
            all_featuresu.insert(feature);
	}	

	if ( edge_label == -1 ){ //merge
	    controller.merge_labels(label2, label1, &node_combine_alg);
        }

    }
    
    vector<vector<double> >	all_features;
    all_featuresu.get_feature_label(all_features, all_labels);	
    
    //printf("Time required to learn RF: %.2f with oob :%f\n", , oob_v.oob_breiman);
    printf("Features generated in %.2fsecs\n",((double)clock() - start) / CLOCKS_PER_SEC);
    feature_mgr->get_classifier()->learn(all_features, all_labels); // number of trees
    printf("Classifier learned \n");

    /*debug*/
    double err=0;
    for(int fcount=0;fcount<all_features.size(); fcount++){
	double predp = feature_mgr->get_classifier()->predict(all_features[fcount]);
	int predl = (predp>0.5)? 1:-1;	
	err+= ((predl==all_labels[fcount])?0:1);	
    }
    printf("accuracy = %.3f\n",100*(1 - err/all_labels.size()));	
    /**/

}

}
