#include<iostream>
#include<fstream>
#include<sstream>
#include<vector>
#include<queue>
#include<list>
#include<math.h>
#include<windows.h>

using namespace std;

const string MEMORY_MANAGEMENT_FILENAME = "MemoryManagement.txt";

const string REFERENCE_FILENAME = "References.txt";

struct Configuration
{
	string referenceFile;
	unsigned missPenalty;
	unsigned dirtyPagePenalty;
	unsigned pageSize;
	int VAbits;
	int PAbits;
	bool debug;
};

void loadConfiguration(istream& is, Configuration& config)
{
	string identifier;
	string data;

	while (getline(is, identifier, '='))
	{
		if (!getline(is, data))
		{
			cerr << "Unexpected end of output";
		}

		istringstream dataStream;
		dataStream.str(data);

		if (identifier == "referenceFile")
		{
			config.referenceFile = data;
		}
		else if (identifier == "missPenalty")
		{
			dataStream >> config.missPenalty;
		}
		else if (identifier == "dirtyPagePenalty")
		{
			dataStream >> config.dirtyPagePenalty;
		}
		else if (identifier == "pageSize")
		{
			dataStream >> config.pageSize;
		}
		else if (identifier == "VAbits")
		{
			dataStream >> config.VAbits;
		}
		else if (identifier == "PAbits")
		{
			dataStream >> config.PAbits;
		}
		else if (identifier == "debug")
		{
			if (data == "true")
				config.debug = true;
			else if (data == "false")
				config.debug = false;
			else
				cerr << "Unrecognized value for debug: "
				     << data << endl;
		}
		else
		{
			cerr << "Unrecongized identifier/value pair: "
				 << identifier << "/" << data << endl;
		}
	}
}

struct VirtualAddress
{
	int page;
	int offset;
};

struct PhysicalAddress
{
	int frame;
	int offset;
};

struct Reference
{
	VirtualAddress va;
	char readOrWrite;
};

struct PageTableEntry
{
	int frameNumber;
	bool validBit;
	bool accessedBit;
	bool dirtyBit;
};

struct Page
{
	int page;
};

struct ProcessControlBlock
{
	unsigned id;
	queue<Reference> references;
	PageTableEntry* pageTable;

	int waitTime;

	bool isDone;
	bool isBlock;
};

struct Frame
{
	int frame;
	Page page;
	ProcessControlBlock* pcb;
};

int loadPCBs(istream& is, vector<ProcessControlBlock*>& pcbs, Configuration& config)
{
	int numberOfPCBs;
	int numberOfReferences;
	is >> numberOfPCBs;
	
	while(!is.eof())
	{
		ProcessControlBlock* pcb = new ProcessControlBlock();
		is >> pcb->id;
		is >> numberOfReferences;
		for (int i = 0; i < numberOfReferences; i++)
		{
			Reference r;
			int va;
			is >> va >> r.readOrWrite;
			r.va.page = va / config.pageSize;
			r.va.offset = va % config.pageSize;
			pcb->references.push(r);
		}

		int pageTableSize = int(pow(2.0, config.PAbits) / config.pageSize);

		pcb->pageTable = new PageTableEntry[pageTableSize];

		for (int page = 0; page < pageTableSize; page++)
		{
			pcb->pageTable[page].frameNumber = -1;
			pcb->pageTable[page].validBit = false;
			pcb->pageTable[page].accessedBit = false;
			pcb->pageTable[page].dirtyBit = false;
		}

		pcb->waitTime = 0;

		pcb->isDone = false;
		pcb->isBlock = false;
		pcbs.push_back(pcb);
	}

	return numberOfPCBs;
}

class MemoryManager
{
public:
	enum Status {FREE, HIT, CLEAN, DIRTY};

	Status status;

	vector<ProcessControlBlock*> pcbs;
	Configuration config;

	queue<ProcessControlBlock*> circularList_pcbs;

	queue<Frame> physicalMemory;
	queue<Page> virtualMemory;

	list<Frame> clock;
	list<Frame>::iterator firstLoadPage;

	int clockSize;

	bool justLoaded;


	MemoryManager(vector<ProcessControlBlock*>& newPcbs, Configuration& newConfig)
		:pcbs(newPcbs), config(newConfig)
	{
		clockSize = int(pow(2.0,config.PAbits) / config.pageSize);

		for (int i = 0; i < clockSize; i++)
		{
			Frame f;
			f.frame = i;
			physicalMemory.push(f);
		}

		for (size_t i = 0; i < pcbs.size(); i++)
		{
			circularList_pcbs.push(pcbs[i]);
		}

		justLoaded = true;
	}

	void run()
	{
		while(!arePCBsDone())
		{
			ProcessControlBlock* runningPCB= circularList_pcbs.front();
			executeProcess(runningPCB);
			while(!runningPCB->isBlock)
			{
				accessMemory(runningPCB);
				// need to unblock them at a certain point;
			}
			cout << "Process: " << runningPCB->id << " waiting: " << runningPCB->waitTime << endl;
			cout << endl;
			unblockPCB(runningPCB);
			circularList_pcbs.pop();
			if(!runningPCB->isDone)
				circularList_pcbs.push(runningPCB);
		}
	}

	bool arePCBsDone()
	{
		bool isDone = true;
		for (int i = 0; i < pcbs.size(); i++)
		{
			if (!pcbs[i]->isDone)
				isDone = false;
		}
		return isDone;
	}

	bool HasFreeFrames()
	{
		if (!physicalMemory.empty())
			return true;
		else
			return false;
	}

	bool pageFault(PageTableEntry* pageTable, Reference& ref)
	{
		if (pageTable[ref.va.page].frameNumber == -1 || 
			pageTable[ref.va.page].validBit == false)
			return true; // miss the page->missPenalty
		else
			return false;
	}

	void addPageToPageTable(Frame& f, Page& p, PageTableEntry* pageTable, Reference& ref)
	{
		pageTable[p.page].frameNumber = f.frame;
		pageTable[p.page].validBit = true;
	}

	void readOrWrite(Page& p, PageTableEntry* pageTable, Reference& ref)
	{
		if (ref.readOrWrite == 'W')
		{
			pageTable[p.page].dirtyBit = true;
		}
		else if (ref.readOrWrite == 'R' && pageTable[p.page].dirtyBit == false)
		{
			pageTable[p.page].dirtyBit = false;
		}
	}

	void unblockPCB(ProcessControlBlock* pcb)
	{
		pcb->isBlock = false;
	}

	void accessMemory(ProcessControlBlock* pcb)
	{
		if (!pcb->references.empty())
		{
			Reference currentReference = pcb->references.front();

			Page page;
			page.page = currentReference.va.page;
			int offset = currentReference.va.offset;
			if (pageFault(pcb->pageTable, currentReference))
			{
				pcb->isBlock = true;

				if (HasFreeFrames())
				{
					pcb->waitTime = config.missPenalty;
					Frame f = physicalMemory.front();
					f.page = page;
					f.pcb = pcb;

					status = FREE;
					addPageToPageTable(f, page, pcb->pageTable, currentReference);

					physicalMemory.pop();
					clock.push_back(f);
				}

				else
				{
					if (justLoaded)
					{
						firstLoadPage = clock.begin();
						justLoaded = false;
					}

					int itr_count = 0;
					while (pcb->pageTable[firstLoadPage->page.page].accessedBit == true)
					{
						firstLoadPage++;
						if (firstLoadPage == clock.end())
							firstLoadPage = clock.begin();
						itr_count++;
						if (itr_count == 8)
							break;
					}

					Frame f = *firstLoadPage;

					if (f.pcb->pageTable[f.page.page].dirtyBit == false)
					{
						status = CLEAN;
						pcb->waitTime = config.missPenalty;
					}
					else if (f.pcb->pageTable[f.page.page].dirtyBit == true)
					{
						f.pcb->pageTable[f.page.page].dirtyBit == false;
						status = DIRTY;
						pcb->waitTime = config.missPenalty + config.dirtyPagePenalty;
					}

					f.pcb->pageTable[f.page.page].validBit = false;

					addPageToPageTable(f, page, pcb->pageTable, currentReference);

					f.page = page;
					f.pcb = pcb;

					*firstLoadPage = f;
					firstLoadPage++;
					if (firstLoadPage == clock.end())
						firstLoadPage = clock.begin();
				}

				// Second Chance Page Replacement
				pcb->pageTable[page.page].accessedBit = true;

				Sleep(pcb->waitTime*1000);
			}

			else
			{
				status = HIT;
				readOrWrite(page, pcb->pageTable, currentReference);
				// Second Chance Page Replacement
				pcb->pageTable[page.page].accessedBit = false;
				//if(pcb->pageTable[page.page].dirtyBit == true)
				//	cout << pcb->id << " " << page.page << endl;
				pcb->references.pop();
			}
			
			cout << "R/W: " << currentReference.readOrWrite << "; ";
			cout << "VA: " << currentReference.va.page * config.pageSize + currentReference.va.offset << "; ";
			cout << "Page: " << currentReference.va.page << "; ";
			cout << "Offset: " << currentReference.va.offset << "; ";
			
			if (status == FREE)
				cout << "Free; ";
			else if (status == HIT)
				cout << "Hit; ";
			else if (status == CLEAN)
				cout << "Clean; ";
			else if (status == DIRTY)
				cout << "Dirty; ";

			PhysicalAddress pa;
			pa.frame = pcb->pageTable[page.page].frameNumber;
			pa.offset = offset;
			cout << "Frame: " << pa.frame << "; ";
			cout << "PA: " << pa.frame * config.pageSize + pa.offset << endl;
		}
		else
		{
			pcb->isBlock = true;
			pcb->isDone = true;
		}
	}

	void executeProcess(ProcessControlBlock* pcb)
	{
		cout << "Running " << pcb->id << endl;
	}

};

int main()
{
	ifstream memoryManagementFile(MEMORY_MANAGEMENT_FILENAME);
	Configuration config;
	loadConfiguration(memoryManagementFile, config);
	memoryManagementFile.close();

	ifstream referenceFile(REFERENCE_FILENAME);
	vector<ProcessControlBlock*> pcbs;
	int numberOfPCBs = loadPCBs(referenceFile, pcbs, config);
	referenceFile.close();
	pcbs.pop_back();

	MemoryManager memoryManager(pcbs, config);
	memoryManager.run();
	system("pause");
	// free frames after the process is done
}