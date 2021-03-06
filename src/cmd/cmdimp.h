#ifndef AUTOLAB_CMDIMP_H_
#define AUTOLAB_CMDIMP_H_

#include "autolab/client.h"

#include "cmdargs.h"

bool init_autolab_client();
int perform_device_flow(Autolab::Client &client);

int show_status(cmdargs &cmd);
int download_asmt(cmdargs &cmd);
int submit_asmt(cmdargs &cmd);
int show_courses(cmdargs &cmd);
int show_assessments(cmdargs &cmd);
int show_assessments_helper(cmdargs &cmd);
int show_problems(cmdargs &cmd);
int show_scores(cmdargs &cmd);
int show_feedback(cmdargs &cmd);
int manage_enrolls(cmdargs &cmd);

/* globals */

extern Autolab::Client client;

#endif /* AUTOLAB_CMDIMP_H_ */
