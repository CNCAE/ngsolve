/**************************************************************************/
/* File:   ngsolve.cpp                                                    */
/* Author: Joachim Schoeberl                                              */
/* Date:   25. Mar. 2000                                                  */
/**************************************************************************/

/*
   Finite element package NGSolve
*/

#include <solve.hpp>
#include <parallelngs.hpp>

#include <tcl.h>
#if TCL_MAJOR_VERSION==8 && TCL_MINOR_VERSION>=4
#define tcl_const const
#else
#define tcl_const
#endif

#include <thread>

using namespace std;
using namespace ngsolve;


#include <nginterface.h>
#include <ngexception.hpp>

namespace netgen
{
    DLL_HEADER extern string ngdir;
}

#ifdef SOCKETS
#include "markus/jobmanager.hpp"
#endif


#ifdef NGS_PYTHON
// #include <dlfcn.h>  // dlopen of python lib ???
#include "../ngstd/python_ngstd.hpp"

void * ptr = (void*)PyOS_InputHook;


class AcquireGIL 
{
    public:
        inline AcquireGIL(){
            state = PyGILState_Ensure();
        }

        inline ~AcquireGIL(){
            PyGILState_Release(state);
        }
    private:
  PyGILState_STATE state;
};

class ReleaseGIL
{
    public:
        inline ReleaseGIL() {
            m_thread_state = PyEval_SaveThread();
        }

        inline ~ReleaseGIL() {
            PyEval_RestoreThread(m_thread_state);
            m_thread_state = NULL;
        }

    private:
        PyThreadState * m_thread_state;
};

class PythonEnvironment : public BasePythonEnvironment {
    public:
    static PythonEnvironment py_env;

    std::thread::id pythread_id;
    std::thread::id mainthread_id;

    // Private constructor
    PythonEnvironment();

    public:
    // Singleton pattern
    static PythonEnvironment &getInstance() {
        return py_env;
    }
    
  virtual void exec(const string s) {
    try{
      bp::exec(s.c_str(), main_namespace, main_namespace);
    }
    catch (bp::error_already_set const &) {
      PyErr_Print();
    }
  }


    void Spawn(string initfile) {
        if(pythread_id != mainthread_id) {
            cout << "Python thread already running!" << endl;
        }
        else {
//             PyEval_ReleaseLock();
            std::thread([](string init_file_) {
                    AcquireGIL gil_lock;
            try{
                py_env.pythread_id = std::this_thread::get_id();
                py_env.exec_file(init_file_.c_str());
                // py_env.exec("from runpy import run_module");
                // py_env.exec("globals().update(run_module('init',globals()))");
            }
            catch (bp::error_already_set const &) {
                PyErr_Print();
            }
            cout << "Python shell finished." << endl;

            py_env.pythread_id = py_env.mainthread_id;
        
              }, initfile).detach();
        }
    }


    virtual ~PythonEnvironment() { }
};

void ExportNgstd();
void ExportNgbla();
void ExportNgfem();
void ExportNgla();
void ExportNgcomp();
void ExportNgsolve();
void ExportNgmpi();

PythonEnvironment::PythonEnvironment() {

    mainthread_id = std::this_thread::get_id();
    pythread_id = std::this_thread::get_id();
    cout << "Init Python environment." << endl;
    Py_Initialize();
    PyEval_InitThreads();
    try{
        main_module = bp::import("__main__");
        main_namespace = main_module.attr("__dict__");
        exec("from sys import path");
        //         exec("from runpy import run_module");
        exec("from os import environ");

        exec("path.append(environ['NETGENDIR'])");
        exec("path.append('.')");

        ExportNgstd();
        ExportNgbla();
        ExportNgfem();
        ExportNgla();
        ExportNgcomp();
        ExportNgsolve();
        ExportNgmpi(); 

        cout << IM(1)
	     << "ngs-python objects are available in ngstd, ngbla, ...\n"
             << "to import the whole bunch of objets enter\n\n"
             << "from ngstd import *\n"
             << "from ngbla import *\n"
             << "from ngfem import *\n"
             << "from ngla import *\n"
             << "from ngcomp import *\n"
             << "from ngsolve import *\n"
             << "from ngmpi import *\n"
             << "dir()\n"
             << endl << endl;

        cout << IM(1) << "To start mpi shell call" << endl << "MpiShell()" << endl << endl;
//         PyEval_ReleaseLock();
    }
    catch(bp::error_already_set const &) {
        PyErr_Print();
    }
}


PythonEnvironment PythonEnvironment::py_env;


BasePythonEnvironment & GetPythonEnvironment () { return PythonEnvironment::getInstance(); }


#endif


// #include "/home/joachim/netgen-mesher/netgen/libsrc/include/meshing.hpp"
// volatile int & running = netgen::multithread.running;
// static bool got_exception = false;


int NGS_PrintRegistered (ClientData clientData,
			 Tcl_Interp * interp,
			 int argc, tcl_const char *argv[])
{
  ngfem::GetIntegrators().Print (cout);
  ngsolve::GetNumProcs().Print (cout);
  ngsolve::GetFESpaceClasses().Print (cout);
  ngsolve::GetPreconditionerClasses().Print (cout);

  return TCL_OK;
}

int NGS_Help (ClientData clientData,
	      Tcl_Interp * interp,
	      int argc, tcl_const char *argv[])
{
  if (argc >= 2)
    {
      string topics = argv[1];

      if (topics == "numprocs")
	{
	  stringstream str;
	  const Array<NumProcs::NumProcInfo*> & npi = GetNumProcs().GetNumProcs();
	  
	  Array<int> sort(npi.Size());
	  Array<string> names(npi.Size());
	  for (int i = 0; i < npi.Size(); i++)
	    {
	      sort[i] = i;
	      names[i] = npi[i]->name;
	    }
	  BubbleSort (names, sort);
	  for (int ii = 0; ii < npi.Size(); ii++)
	    str << npi[sort[ii]]->name << " ";

	  Tcl_SetResult (interp, (char*)str.str().c_str(), TCL_VOLATILE);
	  return TCL_OK;
	}

      stringstream str;

      if (topics == "constant")
	{
	  str << "heapsize = <num bytes>\n"
	      << "   size for optimized memory handler\n\n"
	      << "testout = <filename>\n"
	      << "   filename for testoutput\n\n"
	      << "numthreads = <num>\n"
	      << "   threads for openmp parallelization\n\n"
	      << "geometryorder = <num>\n"
	      << "   curved elements of this polynomial order\n\n"
	      << "refinep = 0|1\n"
	      << "   increase p instead of mesh refinement\n\n"
	      << "refinehp = 0|1\n"
	      << "   h-refinement only for singular elements, otherwise p\n\n"
	      << endl;
	}

      if (topics == "coefficient")
	{
	  str << "define coefficient <name>" << endl;
	  str << "val1, val2, val3, ...." << endl;
	}

      if (topics == "bilinearform")
	{
	  ;
	}

      if (argc >= 3 && strcmp (argv[1], "numproc") == 0)
	{
	  const Array<NumProcs::NumProcInfo*> & npi =
	    GetNumProcs().GetNumProcs();
	  for (int i = 0; i < npi.Size(); i++)
	    {
	      if (strcmp (argv[2], npi[i]->name.c_str()) == 0)
		{
		  npi[i]->printdoc(str);
		}
	    }
	}

      cout << str.str();
      Tcl_SetResult (interp, (char*)str.str().c_str(), TCL_VOLATILE);
    }
  return TCL_OK;
}



NGS_DLL_HEADER shared_ptr<ngsolve::PDE> pde;


#ifdef _NGSOLVE_SOCKETS_HPP
  class SocketOutArchive : public Archive
  {
    Socket & sock;
  public:
    SocketOutArchive (Socket & asock) : sock(asock) { ; }

    virtual bool Output () { return true; }
    virtual bool Input () { return false; }

    virtual Archive & operator & (double & d) 
    {
      sock.Tsend(d);
      return *this; 
    }
    virtual Archive & operator & (int & i) 
    {
      sock.Tsend(i);
      return *this; 
    }
    virtual Archive & operator & (short int & i) 
    {
      sock.Tsend(i);
      return *this; 
    }
    virtual Archive & operator & (unsigned char & i) 
    { 
      sock.Tsend (i);
      return *this; 
    }
    virtual Archive & operator & (bool & b) 
    { 
      return *this; 
    }
    virtual Archive & operator & (string & str) 
    {
      sock.send (str);
      return *this; 
    }
    virtual Archive & operator & (char *& str) 
    {
      sock.send (string (str));
      return *this; 
    }
  };


  class SocketInArchive : public Archive
  {
    Socket & sock;
  public:
    SocketInArchive (Socket & asock) : sock(asock) { ; }
    virtual bool Output () { return false; }
    virtual bool Input () { return true; }
    virtual Archive & operator & (double & d) 
    {
      sock.Trecv (d);
      return *this; 
    }
    virtual Archive & operator & (int & i) 
    {
      sock.Trecv (i);
      return *this; 
    }
    virtual Archive & operator & (short int & i) 
    {
      sock.Trecv (i);
      return *this; 
    }
    virtual Archive & operator & (unsigned char & i) 
    {
      sock.Trecv(i);
      return *this; 
    }
    virtual Archive & operator & (bool & b) { return *this; }

    virtual Archive & operator & (string & str) 
    { 
      sock.recv (str);
      return *this;
    }

    virtual Archive & operator & (char *& str) 
    { 
      string hstr;
      sock.recv (hstr);
      str = new char [hstr.length()+1];
      strcpy (str, hstr.c_str());
      return *this;
    }
  };

#endif

#ifndef WIN32
static pthread_t socket_thread;

void MyRunParallel ( void * (*fun)(void *), void * in)
{
  pthread_attr_t attr;
  pthread_attr_init (&attr);
  pthread_attr_setstacksize(&attr, 1000000);
  pthread_create (&socket_thread, &attr, fun, in);
}



void * SocketThread (void * data)
{
  int port = *(int*)data;
  cout << "NGSolve accepts socket communication on port " << port << endl;
  ServerSocket server (port);

  while (true)
    {
      try
        {
          ServerSocket new_sock;
          server.accept (new_sock);

          while (new_sock.is_valid())
            {
              string str;
              new_sock >> str;
              
              if (str == "")
                break;
              else if (str == "np") 
                new_sock << ToString (pde->GetMeshAccess().GetNP());
              else if (str == "ne") 
                new_sock << ToString (pde->GetMeshAccess().GetNE());
              /*
              else if (str == "mesh") 
                {
                  stringstream str;
                  netgen::mesh -> Save (str);
                  new_sock << str.str();
                }
              */
              else if (str == "pde") 
                {
                  SocketOutArchive archive(new_sock);
                  pde -> DoArchive (archive);
                  cout << "PDE completely sent" << endl;
                }
              else
                {
                  cout << "got string '" << str << "'" << endl;
                  new_sock << "hab dich nicht verstanden";
                }

            }
        }
      catch ( SocketException& ) {}
    }
  return NULL;
}

#endif





int NGS_LoadPDE (ClientData clientData,
		 Tcl_Interp * interp,
		 int argc, tcl_const char *argv[])
{

  if (Ng_IsRunning())
    {
      Tcl_SetResult (interp, (char*)"Thread already running", TCL_STATIC);
      return TCL_ERROR;
    }

  if (argc >= 2)
    {
      try
	{
	  MyMPI_SendCmd ("ngs_pdefile");

	  pde = make_shared<ngsolve::PDE>();
          pde->SetTclInterpreter (interp);

	  // make sure to have lapack loaded (important for newer MKL !!!)
	  Matrix<> a(100), b(100), c(100);
	  a = 1; b = 2; c = a*b | Lapack;

          pde->LoadPDE (argv[1]);
	  pde->PrintReport (*testout);

#ifdef NGS_PYTHON
          cout << "set python mesh" << endl;
          // PythonEnvironment::getInstance()["mesh"] = bp::ptr(&pde->GetMeshAccess(0));
          {
//             AcquireGIL gil_lock;
            PythonEnvironment::getInstance()["pde"] = bp::ptr(&*pde);
          }
#endif

          int port = pde -> GetConstant ("port", true);
          if (port)
            {
              int * hport = new int;
              *hport = port;
#ifndef WIN32
              MyRunParallel (SocketThread, hport);
#endif
            }
	}
      catch (exception & e)
	{
	  cerr << "\n\nCaught exception in NGS_LoadPDE:\n"
	       << typeid(e).name() << endl;
          pde->SetGood (false);
	}
      catch (ngstd::Exception & e)
	{
          pde->SetGood (false);
	  cerr << "\n\nCaught Exception in NGS_LoadPDE:\n"
	       << e.What() << endl;

	  ostringstream ost;
	  ost << "Exception in NGS_LoadPDE: \n " << e.What() << endl;
	  Tcl_SetResult (interp, (char*)ost.str().c_str(), TCL_VOLATILE);
	  return TCL_ERROR;
	}


      catch (netgen::NgException & e)
	{
          pde->SetGood (false);
	  cerr << "\n\nCaught Exception in NGS_LoadPDE:\n"
	       << e.What() << endl;

	  ostringstream ost;
	  ost << "Exception in NGS_LoadPDE: \n " << e.What() << endl;
	  Tcl_SetResult (interp, (char*)ost.str().c_str(), TCL_VOLATILE);
	  return TCL_ERROR;
	}

    }
  return TCL_OK;
}


void * SolveBVP(void *)
{
#ifdef _OPENMP
  if (MyMPI_GetNTasks (MPI_COMM_WORLD) > 1)
    omp_set_num_threads (1);
#endif

#ifdef NGS_PYTHON
//   AcquireGIL gil_lock;
#endif

  try
    {
      if (pde && pde->IsGood())
	pde->Solve();
    }

  catch (exception & e)
    {
      cerr << "\n\ncaught exception in SolveBVP:\n "
	   << typeid(e).name() << endl;
      pde->SetGood (false);
    }
#ifdef _MSC_VER
# ifndef MSVC_EXPRESS
  catch (CException * e)
    {
      TCHAR msg[255];
      e->GetErrorMessage(msg, 255);
      cerr << "\n\ncaught Exception in SolveBVP:\n"
	   << msg << "\n\n";
      pde->SetGood (false);
    }
# endif // MSVC_EXPRESS
#endif
  catch (ngstd::Exception & e)
    {
      cerr << "\n\ncaught Exception in SolveBVP:\n"
	   << e.What() << "\n\n";
      pde->SetGood (false);
    }
  catch (netgen::NgException & e)
    {
      cerr << "\n\ncaught Exception in SolveBVP:\n"
	   << e.What() << "\n\n";
      pde->SetGood (false);
    }

  Ng_SetRunning (0); 
  return NULL;
}



int NGS_SolvePDE (ClientData clientData,
		  Tcl_Interp * interp,
		  int argc, tcl_const char *argv[])
{
  if (Ng_IsRunning())
    {
      Tcl_SetResult (interp, (char*)"Thread already running", TCL_STATIC);
      return TCL_ERROR;
    }

#ifdef NGS_PYTHON
//   AcquireGIL gil_lock;
#endif

  cout << "Solve PDE" << endl;
  Ng_SetRunning (1);

  MyMPI_SendCmd ("ngs_solvepde");

  RunParallel (SolveBVP, NULL);

  return TCL_OK;
}


int NGS_EnterCommand (ClientData clientData,
                      Tcl_Interp * interp,
                      int argc, tcl_const char *argv[])
{
  cout << "Enter command: ";
  string st;
  char ch;
  do
    {
      cin.get(ch);
      st += ch;
    }
  while (ch != '\n');
  cout << "command = " << st << endl;
  if (pde)
    {
      stringstream sstream(st);
      pde->LoadPDE (sstream);
      pde->Solve ();
      pde->PrintReport (*testout);
    }

  return TCL_OK;
}




int NGS_PrintPDE (ClientData clientData,
		  Tcl_Interp * interp,
		  int argc, tcl_const char *argv[])
{
  if (pde)
    {
      if (argc == 1)
	pde->PrintReport(cout);
      else if (argc == 3)
	{
	  if (strcmp (argv[1], "coeffs") == 0)
	    pde->GetCoefficientFunction (argv[2])->PrintReport(cout);
	  else if (strcmp (argv[1], "spaces") == 0)
	    pde->GetFESpace (argv[2])->PrintReport(cout);
	  else if (strcmp (argv[1], "biforms") == 0)
	    pde->GetBilinearForm (argv[2])->PrintReport(cout);
	  else if (strcmp (argv[1], "liforms") == 0)
	    pde->GetLinearForm (argv[2])->PrintReport(cout);
	  else if (strcmp (argv[1], "gridfuns") == 0)
	    pde->GetGridFunction (argv[2])->PrintReport(cout);
	  else if (strcmp (argv[1], "preconds") == 0)
	    pde->GetPreconditioner (argv[2])->PrintReport(cout);
	  else if (strcmp (argv[1], "numprocs") == 0)
	    pde->GetNumProc (argv[2])->PrintReport(cout);
	}
      return TCL_OK;
    }
  Tcl_SetResult (interp, (char*)"No pde loaded", TCL_STATIC);
  return TCL_ERROR;
}

int NGS_SaveSolution (ClientData clientData,
		      Tcl_Interp * interp,
		      int argc, tcl_const char *argv[])
{
  if (argc >= 2)
    {
      if (pde)
	{
	  pde->SaveSolution (argv[1],(argc >= 3 && atoi(argv[2])));
	  return TCL_OK;
	}
    }
  Tcl_SetResult (interp, (char*)"Cannot save solution", TCL_STATIC);
  return TCL_ERROR;
}


int NGS_LoadSolution (ClientData clientData,
		      Tcl_Interp * interp,
		      int argc, tcl_const char *argv[])
{
  if (argc >= 2 && pde)
    {
      pde->LoadSolution (argv[1], (argc >= 3 && atoi(argv[2])));
      return TCL_OK;
    }

  Tcl_SetResult (interp, (char*)"Cannot load solution", TCL_STATIC);
  return TCL_ERROR;
}



int NGS_DumpPDE (ClientData clientData,
		      Tcl_Interp * interp,
		      int argc, tcl_const char *argv[])
{
  if (argc >= 2 && pde)
    {
      TextOutArchive archive (argv[1]);
      pde->DoArchive (archive);
      return TCL_OK;
    }

  Tcl_SetResult (interp, (char*)"Dump error", TCL_STATIC);
  return TCL_ERROR;
}

int NGS_RestorePDE (ClientData clientData,
                         Tcl_Interp * interp,
                         int argc, tcl_const char *argv[])
{
  if (argc >= 2)
    {
      TextInArchive archive (argv[1]);
      pde = make_shared<ngsolve::PDE>();
      pde->DoArchive (archive);

#ifdef NGS_PYTHON
      AcquireGIL gil_lock;
      PythonEnvironment::getInstance()["pde"] = bp::ptr(&*pde);
#endif
      return TCL_OK;
    }

  Tcl_SetResult (interp, (char*)"Dump error", TCL_STATIC);
  return TCL_ERROR;
}


int NGS_SocketLoad (ClientData clientData,
                    Tcl_Interp * interp,
                    int argc, tcl_const char *argv[])
{
  if (argc >= 2)
    {
#ifndef WIN32
      try
        {
          int portnum = atoi (argv[1]);
          cout << "load from port " << portnum;
          
          string hostname = "localhost";
          if (argc >= 3) hostname = argv[2];
              
          ClientSocket socket (portnum, hostname);
          socket << "pde";
          
          SocketInArchive archive (socket);
          pde = make_shared<PDE>();
          pde->DoArchive (archive);
          return TCL_OK;
        }
      catch (SocketException & e)
        {
          cout << "caught SocketException : " << e.What() << endl;
          Tcl_SetResult (interp, (char*) e.What().c_str(), TCL_VOLATILE);
          return TCL_ERROR;
        }
#endif
    }

  Tcl_SetResult (interp, (char*)"load socket error", TCL_STATIC);
  return TCL_ERROR;
}


int NGS_PythonShell (ClientData clientData,
                    Tcl_Interp * interp,
                    int argc, tcl_const char *argv[])
{
#ifdef NGS_PYTHON
  string initfile = netgen::ngdir + dirslash + "init.py";
  cout << "python init file = " << initfile << endl;
  auto py_env = PythonEnvironment::getInstance();
  py_env.Spawn(initfile);
  return TCL_OK;
#else
  cerr << "Sorry, you have to compile ngsolve with Python" << endl;
  return TCL_ERROR;
#endif
}



int NGS_PrintMemoryUsage (ClientData clientData,
			  Tcl_Interp * interp,
			  int argc, tcl_const char *argv[])
{
  // netgen::BaseMoveableMem::Print ();
  
  // netgen::BaseDynamicMem::Print ();

  return TCL_OK;
}



int NGS_PrintTiming (ClientData clientData,
		     Tcl_Interp * interp,
		     int argc, tcl_const char *argv[])
{
  ngstd::NgProfiler::Print (stdout);
  return TCL_OK;
}




int NGS_GetData (ClientData clientData,
		 Tcl_Interp * interp,
		 int argc, tcl_const char *argv[])
{
  static char buf[1000];
  buf[0] = 0;
  stringstream str;

  if (argc >= 2 && pde)
    {
      if (strcmp (argv[1], "constants") == 0)
	{
	  for (int i = 0; i < pde->GetConstantTable().Size(); i++)
	    str << "{ " << pde->GetConstantTable().GetName(i) << " = " 
		<< pde->GetConstantTable()[i] << " } ";
	}

      if (strcmp (argv[1], "variableswithval") == 0)
	{
	  for (int i = 0; i < pde->GetVariableTable().Size(); i++)
	    str << "{ " << pde->GetVariableTable().GetName(i) << " = " 
		<< *pde->GetVariableTable()[i] << " } ";
	}

      if (strcmp (argv[1], "variables") == 0)
	{
	  for (int i = 0; i < pde->GetVariableTable().Size(); i++)
	    str << pde->GetVariableTable().GetName(i) << " ";
	}

      if (strcmp (argv[1], "variablesval") == 0)
	{
	  for (int i = 0; i < pde->GetVariableTable().Size(); i++)
	    str << "val" << *pde->GetVariableTable()[i] 
		<<  "name" << pde->GetVariableTable().GetName(i) << " ";
	}

      if (strcmp (argv[1], "coefficients") == 0)
	{
	  for (int i = 0; i < pde->GetCoefficientTable().Size(); i++)
	    str << pde->GetCoefficientTable().GetName(i) << " ";
	}

      if (strcmp (argv[1], "spaces") == 0)
	{
	  for (int i = 0; i < pde->GetSpaceTable().Size(); i++)
	    str << pde->GetSpaceTable().GetName(i) << " ";
	}

      if (strcmp (argv[1], "gridfunctions") == 0)
	{
	  for (int i = 0; i < pde->GetGridFunctionTable().Size(); i++)
	    str << pde->GetGridFunctionTable().GetName(i) << " ";
	}

      if (strcmp (argv[1], "linearforms") == 0)
	{
	  for (int i = 0; i < pde->GetLinearFormTable().Size(); i++)
	    str << pde->GetLinearFormTable().GetName(i) << " ";
	}

      if (strcmp (argv[1], "bilinearforms") == 0)
	{
	  for (int i = 0; i < pde->GetBilinearFormTable().Size(); i++)
	    str << pde->GetBilinearFormTable().GetName(i) << " ";
	}

      if (strcmp (argv[1], "preconditioners") == 0)
	{
	  for (int i = 0; i < pde->GetPreconditionerTable().Size(); i++)
	    str << pde->GetPreconditionerTable().GetName(i) << " ";
	}

      if (strcmp (argv[1], "numprocs") == 0)
	{
	  for (int i = 0; i < pde->GetNumProcTable().Size(); i++)
	    str << pde->GetNumProcTable().GetName(i) << " ";
	}

      if (strcmp (argv[1], "evaluatefiles") == 0)
	{
	  string auxstring = pde->GetEvaluateFiles();
          size_t i=0;
	  while(i<auxstring.size())
	    {
              i = auxstring.find('\\',i);
	      if(i>=0 && i<auxstring.size())
		auxstring.replace(i,1,"\\\\");
	      else
		i = auxstring.size();
	      i+=2;

	    }
	  str << auxstring;
	}




      if (strcmp (argv[1], "numcoefficients") == 0)
	{
	  sprintf (buf, "%d", pde->GetCoefficientTable().Size());
	}
      else if (strcmp (argv[1], "coefficientname") == 0)
	{
	  sprintf (buf, "%s",
		   pde->GetCoefficientTable().GetName(atoi(argv[2])).c_str());
	}



      if (strcmp (argv[1], "numspaces") == 0)
	{
	  sprintf (buf, "%d", pde->GetSpaceTable().Size());
	}
      else if (strcmp (argv[1], "spacename") == 0)
	{
	  sprintf (buf, "%s",
		   pde->GetSpaceTable().GetName(atoi(argv[2])).c_str());
	}
      else if (strcmp (argv[1], "spacetype") == 0)
	{
	  cout << "ask space type " << endl;
	  auto space = pde->GetFESpace(argv[2]);
	  cerr << "space = " << space << endl;
	  if (space)  sprintf (buf, "%s", space->GetClassName().c_str());
	  else sprintf (buf, "Nodal");
	}
      else if (strcmp (argv[1], "spaceorder") == 0)
	{
	  auto space = pde->GetFESpace(argv[2]);
	  if (space)  sprintf (buf, "%d", space->GetOrder());
	  else sprintf (buf, "1");
	}
      else if (strcmp (argv[1], "spacedim") == 0)
	{
	  auto space = pde->GetFESpace(argv[2]);
	  if (space)  sprintf (buf, "%d", space->GetDimension());
	  else sprintf (buf, "1");
	}
      else if (strcmp (argv[1], "setspace") == 0)
	{
	  const char * name = argv[2];
	  // const char * type = argv[3];
	  ngstd::Flags flags;
	  flags.SetFlag ("order", atoi (argv[4]));
	  flags.SetFlag ("dim", atoi (argv[5]));
	  pde->AddFESpace (name, flags);
	}


      else if (strcmp (argv[1], "numgridfunctions") == 0)
	{
	  sprintf (buf, "%d", pde->GetGridFunctionTable().Size());
	}
      else if (strcmp (argv[1], "gridfunctionname") == 0)
	{
	  sprintf (buf, "%s",
		   pde->GetGridFunctionTable().GetName(atoi(argv[2])).c_str());
	}
      else if (strcmp (argv[1], "gridfunctionspace") == 0)
	{
	  shared_ptr<ngcomp::GridFunction> gf = pde->GetGridFunction(argv[2]);
	  if (gf)
	    sprintf (buf, "%s", 
		     gf->GetFESpace().GetName().c_str()); 
	  else
	    sprintf (buf, "v");
	}

      else if (strcmp (argv[1], "numbilinearforms") == 0)
	{
	  sprintf (buf, "%d", pde->GetBilinearFormTable().Size());
	}
      else if (strcmp (argv[1], "bilinearformname") == 0)
	{
	  sprintf (buf, "%s",
		   pde->GetBilinearFormTable().GetName(atoi(argv[2])).c_str());
	}

      else if (strcmp (argv[1], "numlinearforms") == 0)
	{
	  sprintf (buf, "%d", pde->GetLinearFormTable().Size());
	}
      else if (strcmp (argv[1], "linearformname") == 0)
	{
	  sprintf (buf, "%s",
		   pde->GetLinearFormTable().GetName(atoi(argv[2])).c_str());
	}

      else if (strcmp (argv[1], "numbilinearformcomps") == 0)
	{
	  sprintf (buf, "%d", pde->GetBilinearForm(argv[2])->NumIntegrators());
	}
      else if (strcmp (argv[1], "bilinearformcompname") == 0)
	{
	  sprintf (buf, "%s",
		   pde->GetBilinearForm(argv[2])->GetIntegrator(atoi(argv[3]))->Name().c_str());
	}

      else if (strcmp (argv[1], "numlinearformcomps") == 0)
	{
	  sprintf (buf, "%d", pde->GetLinearForm(argv[2])->NumIntegrators());
	}
      else if (strcmp (argv[1], "linearformcompname") == 0)
	{
	  sprintf (buf, "%s",
		   pde->GetLinearForm(argv[2])->GetIntegrator(atoi(argv[3]))->Name().c_str());
	}


    }
  else
    {
      sprintf (buf, "0");
    }
  str << buf;
  Tcl_SetResult (interp, (char*)str.str().c_str(), TCL_VOLATILE);
  return TCL_OK;
}


#ifdef NOT_WORKING
// from multibody ?
char playanimfile[256];
extern void PlayAnimFile(const char* name, int speed, int maxcnt);
/*
void * PlayAnim(void *)
{
  PlayAnimFile(playanimfile);

  running = 0;
  return NULL;
}
*/


int NGS_PlayAnim (ClientData clientData,
		  Tcl_Interp * interp,
		  int argc, tcl_const char *argv[])
{
  /*
  if (running)
    {
      Tcl_SetResult (interp, "Thread already running", TCL_STATIC);
      return TCL_ERROR;
    }
  running = 1;
  */
  int speed = 1;
  int maxcnt = 10;
  const char* name = "geom";
  if (argc >= 4)
    {
      speed = atoi(argv[1]);
      maxcnt = atoi(argv[2]);
      name = argv[3];
    }
  PlayAnimFile(name, speed, maxcnt);
  //  RunParallel (PlayAnim, NULL);
  return TCL_OK;
}
#endif





int NGS_Set (ClientData clientData,
	     Tcl_Interp * interp,
	     int argc, tcl_const char *argv[])
{
  if (argc >= 3 && strcmp (argv[1], "time") == 0)
    {
      double time = double (atof (argv[2])) * 1e-6;
      cout << "NGS time = " << time << endl;
      if (pde)
	{
	  pde->GetVariable ("t", 1) = time;
	}
    }
  return TCL_OK;
}



extern "C" int NGSolve_Init (Tcl_Interp * interp);
extern "C" void NGSolve_Exit ();

// tcl package dynamic load
extern "C" int NGS_DLL_HEADER Ngsolve_Init (Tcl_Interp * interp)
{
  return NGSolve_Init(interp);
}


// tcl package dynamic load
extern "C" int NGS_DLL_HEADER Ngsolve_Unload (Tcl_Interp * interp)
{
  MyMPI_SendCmd ("ngs_exit");
  pde.reset();
  return TCL_OK;
}



namespace ngsolve { 
  // namespace bvp_cpp { extern int link_it; }
  namespace numprocee_cpp { extern int link_it; }
}

/*
namespace ngfem {
  namespace bdbequations_cpp { extern int link_it; }
  // extern int link_it_h1hofefo;
}
*/
namespace ngcomp {
  extern int link_it_hdivhofes;
}



int NGSolve_Init (Tcl_Interp * interp)
{
  cout << "NGSolve-" << VERSION << endl;

#ifdef LAPACK
  cout << "Using Lapack" << endl;
#else
  cout << "sorry, no lapack" << endl; 
#endif


#ifdef USE_PARDISO
  cout << "Including sparse direct solver Pardiso" << endl;
#endif

#ifdef USE_SUPERLU
  cout << "Including sparse direct solver SuperLU by Lawrence Berkeley National Laboratory" << endl;
#endif

#ifdef PARALLEL
  MyMPI_SendCmd ("ngs_loadngs");
  MPI_Comm_dup ( MPI_COMM_WORLD, &ngs_comm);      
  NGSOStream::SetGlobalActive (true);
#endif

  if (getenv ("NGSPROFILE"))
    NgProfiler::SetFileName (string("ngs.prof"));
  
#ifdef _OPENMP
#ifdef PARALLEL
  if (MyMPI_GetNTasks(MPI_COMM_WORLD) > 1)
    omp_set_num_threads (1);
#endif
  cout << "Running OpenMP - parallel using " << omp_get_max_threads() << " thread(s)" << endl;
  // cout << "OpenMP Version " << _OPENMP << endl;
  cout << "(number of threads can be changed by setting OMP_NUM_THREADS)" << endl;
#endif
  

#ifdef VTRACE
  cout << "Vampirtrace - enabled" << endl;
  if (MyMPI_GetNTasks(MPI_COMM_WORLD) == 1 && omp_get_num_threads() > 1)
    {
      cout << " ... thus setting num_omp_threads to 1" << endl;
      omp_set_num_threads (1);
    }
#endif


  
#ifdef SOCKETS
  if(netgen::serversocketmanager.Good())
    serverjobmanager.Reset(new ServerJobManager(netgen::serversocketmanager,pde,ma,interp)); //!

#endif


  

#ifdef NGS_PYTHON

  // void * handle = dlopen ("libpython3.2mu.so", RTLD_LAZY | RTLD_GLOBAL);

  string initfile = netgen::ngdir + dirslash + "init.py";
  cout << "python init file = " << initfile << endl;
  auto py_env = PythonEnvironment::getInstance();

  {
    bp::scope sc(py_env.main_module);
    bp::def ("SetDefaultPDE", 
             FunctionPointer([](shared_ptr<PDE> apde) 
                             {  
                               pde = apde;
                               pde->GetMeshAccess().SelectMesh();
                               Ng_Redraw();
                               return; 
                             }));
    bp::def ("Redraw", 
             FunctionPointer([]() {Ng_Redraw();}));
  }
  
  if (MyMPI_GetId() == 0) {
      PyEval_ReleaseLock();
      py_env.Spawn(initfile);
  }

#endif


  Tcl_CreateCommand (interp, "NGS_PrintRegistered", NGS_PrintRegistered,
		     (ClientData)NULL,
		     (Tcl_CmdDeleteProc*) NULL);

  Tcl_CreateCommand (interp, "NGS_Help", NGS_Help,
		     (ClientData)NULL,
		     (Tcl_CmdDeleteProc*) NULL);

  Tcl_CreateCommand (interp, "NGS_LoadPDE", NGS_LoadPDE,
		     (ClientData)NULL,
		     (Tcl_CmdDeleteProc*) NULL);

  Tcl_CreateCommand (interp, "NGS_SolvePDE", NGS_SolvePDE,
		     (ClientData)NULL,
		     (Tcl_CmdDeleteProc*) NULL);

  Tcl_CreateCommand (interp, "NGS_EnterCommand", NGS_EnterCommand,
		     (ClientData)NULL,
		     (Tcl_CmdDeleteProc*) NULL);

  Tcl_CreateCommand (interp, "NGS_PrintPDE", NGS_PrintPDE,
		     (ClientData)NULL,
		     (Tcl_CmdDeleteProc*) NULL);

  Tcl_CreateCommand (interp, "NGS_SaveSolution", NGS_SaveSolution,
		     (ClientData)NULL,
		     (Tcl_CmdDeleteProc*) NULL);

  Tcl_CreateCommand (interp, "NGS_LoadSolution", NGS_LoadSolution,
		     (ClientData)NULL,
		     (Tcl_CmdDeleteProc*) NULL);

  Tcl_CreateCommand (interp, "NGS_DumpPDE", NGS_DumpPDE,
		     (ClientData)NULL,
		     (Tcl_CmdDeleteProc*) NULL);

  Tcl_CreateCommand (interp, "NGS_RestorePDE", NGS_RestorePDE,
		     (ClientData)NULL,
		     (Tcl_CmdDeleteProc*) NULL);

  Tcl_CreateCommand (interp, "NGS_SocketLoad", NGS_SocketLoad,
		     (ClientData)NULL,
		     (Tcl_CmdDeleteProc*) NULL);


  Tcl_CreateCommand (interp, "NGS_PythonShell", NGS_PythonShell,
		     (ClientData)NULL,
		     (Tcl_CmdDeleteProc*) NULL);

  Tcl_CreateCommand (interp, "NGS_PrintMemoryUsage", NGS_PrintMemoryUsage,
		     (ClientData)NULL,
		     (Tcl_CmdDeleteProc*) NULL);

  Tcl_CreateCommand (interp, "NGS_PrintTiming", NGS_PrintTiming,
		     (ClientData)NULL,
		     (Tcl_CmdDeleteProc*) NULL);





  Tcl_CreateCommand (interp, "NGS_GetData", NGS_GetData,
		     (ClientData)NULL,
		     (Tcl_CmdDeleteProc*) NULL);

  Tcl_CreateCommand (interp, "NGS_Set", NGS_Set,
		     (ClientData)NULL,
		     (Tcl_CmdDeleteProc*) NULL);


  /*
  const Array<NumProcs::NumProcInfo*> & npi = GetNumProcs().GetNumProcs();

  Array<int> sort(npi.Size());
  Array<string> names(npi.Size());
  for (int i = 0; i < npi.Size(); i++)
    {
      sort[i] = i;
      names[i] = npi[i]->name;
    }
  BubbleSort (names, sort);

  Tcl_Eval (interp, "menu .ngmenusolvehelpnp");
  for (int ii = 0; ii < npi.Size(); ii++)
    {
      int i = sort[ii];
      string command =
	".ngmenusolvehelpnp add command -label \"numproc " + npi[i]->name + "\" \
	-command { tk_messageBox -title \"Help\" -message  [ NGS_Help  numproc " + npi[i]->name + "] -type ok }" ;

      Tcl_Eval (interp, (char *) command.c_str());
    }
  */

  // trick for forcing linking static libs
  // ngsolve::bvp_cpp::link_it = 0;
  // ngfem::bdbequations_cpp::link_it = 0;
  // ngfem::link_it_h1hofefo = 0;
  ngsolve::numprocee_cpp::link_it = 0;



  return TCL_OK;
}


/*
void NGSolve_Exit ()
{
  cout << "NGSolve says good bye" << endl;
  // delete pde;
  // delete ma;
  // ma = 0;

  // Parallel_Exit();
}
*/






#ifdef PARALLEL
				    

void * SolveBVP2(void *)
{
#ifdef _OPENMP
  omp_set_num_threads (1);
#endif

  if (pde && pde->IsGood())
    pde->Solve();

  Ng_SetRunning (0); 
  return NULL;
}


extern "C" void NGS_ParallelRun (const string & message);

void NGS_ParallelRun (const string & message)
{
#ifdef _OPENMP
  omp_set_num_threads (1);
#endif

  NGSOStream::SetGlobalActive (false);

  // make sure to have lapack loaded (important for newer MKL !!!)
  Matrix<> a(100), b(100), c(100);
  a = 1; b = 2; c = a*b | Lapack;


  if (getenv ("NGSPROFILE"))
    {
      stringstream filename;
      filename << "ngs.prof." << MyMPI_GetId (MPI_COMM_WORLD);
      NgProfiler::SetFileName (filename.str());
    }

  if ( message == "ngs_loadngs" )
    {
      MPI_Comm_dup ( MPI_COMM_WORLD, &ngs_comm);      
    }

  else if ( message == "ngs_pdefile" )
    {
      // ngs_comm = MPI_COMM_WORLD;

      // ma.Reset(new ngcomp::MeshAccess());
      pde = make_shared<PDE>(); // .Reset(new PDE);

      /*
      // transfer file contents, not filename
      string pdefiledata;
      string filename, pde_directory;
      cout << "ready to receive" << endl;
      MyMPI_Recv (filename, 0);
      MyMPI_Recv (pde_directory, 0);
      MyMPI_Recv (pdefiledata, 0);

      istringstream pdefile (pdefiledata);

      pde->SetDirectory(pde_directory);
      pde->SetFilename(filename);
      pde -> LoadPDE (pdefile, false, 0);
      */

      string dummy;
      pde -> LoadPDE (dummy, false, 0);
#ifdef NGS_PYTHON
          cout << "set python mesh" << endl;
          {
//             AcquireGIL gil_lock;
            PythonEnvironment::getInstance()["pde"] = bp::ptr(&*pde);
          }
#endif

    } 

  else if ( message == "ngs_solvepde" )
    {
      RunParallel (SolveBVP2, NULL);
    }

  else if ( message == "ngs_exit" )
    {
      pde.reset(); 
    }

#ifdef NGS_PYTHON
  else if (message.substr(0,7) == "ngs_py " ) 
    {
      string command = message.substr(7);
      AcquireGIL gil_lock;
      PythonEnvironment & py_env = PythonEnvironment::getInstance();
      py_env.exec(command);
    }
#endif
  return;
}


void Parallel_Exit ()
{
  ;
}


#endif





#ifdef WIN32
void * __cdecl my_operator_new_replacement(size_t _count)
{
	void * p = operator new(_count);
//	cout << "alloc, cnt = " << _count << ", ptr = " << p << endl;
  return p;
  }

void __cdecl my_operator_delete_replacement(void * _ptr)
{
//  cout << "delete, ptr = " << _ptr << endl;
  delete (_ptr);
}

void * __cdecl my_operator_new_array_replacement(size_t _count)
{
		void * p = operator new[] (_count);
//	cout << "alloc [], cnt = " << _count << ", ptr = " << p << endl;
  return p;

//   return operator new[](_count);
}

void __cdecl my_operator_delete_array_replacement(void * _ptr)
{
//  cout << "delete[], ptr = " << _ptr << endl;
  delete [] (_ptr);
}
#endif
