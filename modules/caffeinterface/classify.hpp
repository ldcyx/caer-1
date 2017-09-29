/* Caffe Interface for deep learning
 *  Author: federico.corradi@inilabs.com
 */

#ifndef __CLASSIFY_H
#define __CLASSIFY_H

#include <caffe/caffe.hpp>
#include <opencv2/core/core.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/imgproc/imgproc.hpp>
#include <algorithm>
#include <iosfwd>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include <libcaer/events/frame.h>
#include <fstream>
#include <string>
#include <boost/circular_buffer.hpp>

using namespace caffe;
// NOLINT(build/namespaces)
using std::string;

/* Pair (label, confidence) representing a prediction. */
typedef std::pair<string, float> Prediction;

class MyCaffe {
private:
	char * file_i;
	void SetMean(const string& mean_file);
	std::vector<float> Predict(const cv::Mat& img,
			bool showactivations);
	void WrapInputLayer(std::vector<cv::Mat>* input_channels);
	void Preprocess(const cv::Mat& img, std::vector<cv::Mat>* input_channels);
	shared_ptr<Net<float> > net_;
	cv::Size input_geometry_;
	int num_channels_;
	cv::Mat mean_;
	std::vector<string> labels_;
	boost::circular_buffer<Prediction> lowpassed;
public:

	void Classifier(const string& model_file, const string& trained_file,
			const string& mean_file, const string& label_file);
	std::vector<Prediction> Classify(const cv::Mat& img, int N,
			 bool showactivations);
	void file_set(caerFrameEventPacketConst frameIn, bool thr, bool printOut,
		bool showactivations, bool norminput);
	void init_network(int lowPassNum);
};

#endif
