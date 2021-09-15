#include "TMVA/RModelProfiler.hxx"

namespace TMVA{
namespace Experimental{
namespace SOFIE{

   RModelProfiler::RModelProfiler(RModel&& model) : RModel(std::move(model)) { 
      fNeededStdLib.insert("chrono");           // for timing operators
      fNeededStdLib.insert("unordered_map");    // for storing profiling results
      fNeededStdLib.insert("string");           // operator names
   }

   void RModelProfiler::GenerateUtilityFunctions() {
      // Generation of 'GetOpAvgTime()'
      fGC +=
         "ProfilerResult GetOpAvgTime() {\n"
         "\tif (profiler_results.size() == 0) {\n"
         //"\t\tthrow std::runtime_error(\"TMVA_SOFIE_" + fName + ": No profiling data. Run 'infer' at least once in order to collect profiling data.\");\n"
         "\t\treturn {};\n"
         "\t}\n"
         "\t\n"
         "\t// Accumulation phase\n"
         "\tProfilerResult avg = profiler_results[0];\n"
         "\tfor (size_t i = 1; i < profiler_results.size(); ++i) {\n"
         "\t\tfor (auto&& pair : profiler_results[i]) {\n"
         "\t\t\tavg[pair.first] += pair.second;\n"
         "\t\t}\n"
         "\t}\n"
         "\t\n"
         "\t// Normalization phase\n"
         "\tfor (auto& pair : avg) {\n"
         "\t\tpair.second /= profiler_results.size();\n"
         "\t}\n"
         "\treturn avg;\n"
         "}\n";
   }

   void RModelProfiler::Generate(){
      Initialize();
      fGC += "//Code for profiling and benchmarking purposes.\n";
      fGC += ("//Code generated automatically by TMVA for Inference of Model file [" + fFileName + "] at [" + fParseTime.substr(0, fParseTime.length()-1) +"] \n");
      for (auto& i: fNeededStdLib) {
         fGC += "#include<" + i + ">\n";
      }
      fGC += ("namespace TMVA_SOFIE_" + fName + "{\n");
      if (!fNeededBlasRoutines.empty()) {
         fGC += ("namespace BLAS{\n");
         for (auto &routine : fNeededBlasRoutines) {
            if (routine == "Gemm") {
               fGC += ("\textern \"C\" void sgemm_(const char * transa, const char * transb, const int * m, const int * n, const int * k,\n"
                       "\t                       const float * alpha, const float * A, const int * lda, const float * B, const int * ldb,\n"
                       "\t                       const float * beta, float * C, const int * ldc);\n");
            } else if (routine == "Gemv") {
               fGC += ("\textern \"C\" void sgemv_(const char * trans, const int * m, const int * n, const float * alpha, const float * A,\n"
                       "\t                       const int * lda, const float * X, const int * incx, const float * beta, const float * Y, const int * incy);\n");
            } else if (routine == "Axpy") {
               fGC += ("\textern \"C\" void saxpy_(const int * n, const float * alpha, const float * x,\n"
                       "\t                         const int * incx, float * y, const int * incy);\n");
            }
         }
         fGC += ("}//BLAS\n");
      }

      // Every time 'infer' is called every operator gets timed in this variable
      fGC += "// Maps an operator name to its execution time in a run.\n";
      fGC += "using ProfilerResult = std::unordered_map<std::string,double>;\n";
      fGC += "std::vector<ProfilerResult> profiler_results;\n";

      if (UtilityFunctionsGeneration) {
         GenerateUtilityFunctions();
      }
      fGC += "\n";

      for (auto& i: fInitializedTensors){
         if (i.second.fType == ETensorType::FLOAT){
            size_t length = 1;
            for (auto & dim: i.second.fShape){
               length *= dim;
            }
            fGC += "float tensor_" + i.first + "[" + std::to_string(length) + "] = {";
            std::shared_ptr<float> data = std::static_pointer_cast<float>(i.second.fData);
            std::stringstream floats;
            for (size_t idx = 0; idx < length-1; idx++){
               floats << std::setprecision(std::numeric_limits<float>::max_digits10) << data.get()[idx] << ", ";
            }
            floats << std::setprecision(std::numeric_limits<float>::max_digits10) << data.get()[length-1];
            fGC += floats.str() +"};\n";
         }
      }
      for (auto&i: fIntermediateTensorInfos){
         if (i.second.type == ETensorType::FLOAT){
            size_t length = 1;
            for (auto & dim: i.second.shape){
               length *= dim;
            }
            fGC += "float tensor_" + i.first + "[" + std::to_string(length) + "];\n";
         }
      }

      if (fOutputTensorNames.size() == 1){
         auto f = fIntermediateTensorInfos.find(fOutputTensorNames[0]);
         if (f == fIntermediateTensorInfos.end()){
            throw std::runtime_error("TMVA-SOFIE: output tensor " + fOutputTensorNames[0] + " not found when trying to get its info");
         }else{
            if (f->second.type == ETensorType::FLOAT){
               fGC += "std::vector<float> ";
            }
         }
      }else{
         std::cout << fOutputTensorNames.size() << std::endl;
         throw std::runtime_error("TMVA-SOFIE: More than 1 output tensor is not yet supported");
      }

      fGC += "infer(";
      for (auto& i: fReadyInputTensorInfos){
         size_t length = 1;
         for (auto& dim: i.second.shape){
            length *= dim;
         }
         if (i.second.type == ETensorType::FLOAT){
         fGC += "float* tensor_" + i.first + ",";
         }
      }
      fGC.pop_back(); //remove last ","
      fGC += "){\n";

      // Creating variables for timing
      fGC += "\tstd::chrono::microseconds elapsed;\n";
      fGC += "\tstd::chrono::steady_clock::time_point tp_start;\n";
      fGC += "\tProfilerResult current_execution;\n";

      for (size_t id = 0; id < fOperators.size(); id++){
         // Starting timer
         fGC += "\ttp_start = std::chrono::steady_clock::now();\n";
         fGC += (fOperators[id]->Generate(std::to_string(id)));
         // Stopping timer
         fGC += "\tcurrent_execution[\"" + fOperators[id]->name + "\"] = std::chrono::duration_cast<std::chrono::microseconds>(\n";
         fGC += "\t\tstd::chrono::steady_clock::now() - tp_start).count() / 1e0;\n";
      }
      fGC += "\tprofiler_results.push_back(std::move(current_execution));\n";
      if (fOutputTensorNames.size() == 1){
         fGC += "\tstd::vector<float> ret (tensor_" + fOutputTensorNames[0] + ", tensor_" + fOutputTensorNames[0] + " + sizeof(tensor_" +
               fOutputTensorNames[0] + ") / sizeof(tensor_" + fOutputTensorNames[0] + "[0]));\n";
         fGC += "\treturn ret;\n";
      }
      fGC += "}\n";
      fGC += ("} //TMVA_SOFIE_" + fName + "\n");
   }

}//SOFIE
}//Experimental
}//TMVA
