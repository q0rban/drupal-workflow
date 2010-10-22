<?php
// $Id: workflow.inc,v 1.1.2.1 2010/09/02 20:51:43 q0rban Exp $

/**
 * @file
 * Worfklow API and CRUD functions.
 */

/**
 * A bit flag used to let us know if an object is in the database.
 */
define('WORKFLOW_IN_DATABASE', 0x01);

/**
 * A bit flag used to let us know if an object is a 'default' in code.
 */
define('WORKFLOW_IN_CODE', 0x02);

/**
 * Load function.
 *
 * @param $name
 *   The machine name of the workflow to load.
 * @return $workflow
 *   Object representing the workflow.
 */
function workflow_load($name) {
  $workflows = workflow_load_all();

  return empty($workflows[$name]) ? FALSE : $workflows[$name];
}

/**
 * Get all workflows.
 *
 * @param $reset
 *   Boolean to clear the cached workflows.
 * @return
 *   An array of workflows.
 */
function workflow_load_all($reset = FALSE) {
  static $workflows;

  if (is_null($workflows) || $reset) {
    // See if we have a populated cache first.
    if (!$reset && $cache = cache_get('workflows')) {
      $workflows = $cache->data;
      return $workflows;
    }

    $workflows = array();

    // Load all workflows from the database.
    $result = db_query("SELECT * FROM {workflows} ORDER BY label ASC");
    while ($workflow = db_fetch_object($result)) {
      // Sanitize the label.
      $workflow->label = check_plain(t($workflow->label));
      // Unserialize the options.
      $workflow->options = unserialize($workflow->options);

      // Load up the states.
      $workflow->states = workflow_get_db_states($workflow->name);

      $workflow->export_type = WORKFLOW_IN_DATABASE; // 0001
      $workflows[$workflow->name] = $workflow;
    }

    // Load up any workflows from code and save them to the database.
    $default_workflows = module_invoke_all('workflow_default_workflows');
    drupal_alter('workflow_default_workflows', $default_workflows);

    foreach ($default_workflows as $workflow) {
      if (empty($workflows[$workflow->name])) {
        $workflow->export_type = WORKFLOW_IN_CODE; // 0010
        $workflows[$workflow->name] = $workflow;
      }
      else {
        // Sets export_type to 0011 (In database and code, aka OVERRIDDEN).
        $workflow->export_type |= WORKFLOW_IN_CODE;
      }
    }

    drupal_alter('workflows', $workflows);

    cache_set('workflows', $workflows);
  }

  return $workflows;
}

/**
 * Get all workflow labels.
 *
 * @return
 *   An array of workflow names.
 */
function workflow_load_all_labels() {
  static $labels;

  if (is_null($labels)) {
    $labels = array();

    foreach (workflow_load_all() as $key => $workflow) {
      $labels[$key] = $workflow->label;
    }
  }

  return $labels;
}

/**
 * Resets the workflow cache and reloads all workflow objects.
 */
function workflow_invalidate_cache() {
  workflow_load_all(TRUE);
}

/**
 * Write the transition roles for a workflow state.
 *
 * @param $from_state
 *   The machine name of the state being transitioned from.
 * @param $transitions
 *   An array of allowed roles, keyed by the destination state.
 */
function workflow_transitions_write($from_state, $transitions) {
  // Clean up existing
  db_query("DELETE FROM {workflow_transitions} WHERE state_name = '%s'", $from_state);

  foreach ($transitions as $to_state => $roles) {
    $transition = array();
    $transition['state_name'] = $from_state;
    $transition['target_state_name'] = $to_state;
    $transition['roles'] = implode(',', $roles);

    $record = drupal_write_record('workflow_transitions', $transition);
  }

  // Invalidate the workflow cache.
  workflow_invalidate_cache();

  return $record;
}

/**
 * Tell caller whether a state is a protected system state, such as the creation state.
 *
 * @param $state
 *   The label of the state to test
 * @return
 *   TRUE if the state is a system state.
 */
function workflow_is_system_state($state) {
  static $states;
  if (!isset($states)) {
    $states = array(t('(creation)') => TRUE);
  }
  return isset($states[$state]);
}

/**
 * Given the machine name of a workflow, return its label.
 *
 * @param $name
 *   The machine name of the workflow.
 * @return
 *   The workflow label.
 */
function workflow_get_label($name) {
  $workflow = workflow_load($name);
  return $workflow->label;
}

/**
 * Create a workflow and its (creation) state.
 *
 * @param $workflow
 *   An array of values needed to create a workflow.
 * @return
 *   The saved workflow object.
 */
function workflow_create($workflow) {
  $default = array('options' => array('comment_log_node' => 1, 'comment_log_tab' => 1));
  $workflow += $default;

  $workflow = (object) $workflow;

  if (drupal_write_record('workflows', $workflow)) {
    if (empty($workflow->states)) {
      $state = new stdClass();
      $state->name = _workflow_creation_state($workflow->name);
      $state->workflow_name = $workflow->name;
      $state->label = t('(creation)');
      $state->sysid = WORKFLOW_CREATION;
      $state->weight = WORKFLOW_CREATION_DEFAULT_WEIGHT;
      workflow_state_save($state);
    }
    else {
      // Save each workflow state.
      array_walk($workflow->states, 'workflow_state_save');
    }

    if (isset($workflow->node_types)) {
      foreach ($workflow->node_types as $type) {
        workflow_node_type_save($workflow->name, $type);
      }
    }

    // Workflow creation affects tabs (local tasks), so force menu rebuild.
    menu_rebuild();
  }

  return $workflow;
}

/**
 * Update an existing workflow.
 *
 * @param $workflow
 *   The workflow object to update.
 * @return
 *   The updated workflow object.
 */
function workflow_update($workflow) {
  $workflow = (object) $workflow;
  $workflow->tab_roles = is_array($workflow->tab_roles) ? implode(',', $workflow->tab_roles) : $workflow->tab_roles;
  $workflow = drupal_write_record('workflows', $workflow, 'wid');

  // Workflow name change affects tabs (local tasks), so force menu rebuild.
  menu_rebuild();

  return $workflow;
}

/**
 * Delete a workflow from the database. Deletes all states,
 * transitions and node type mappings, too. Removes workflow state
 * information from nodes participating in this workflow.
 *
 * @param $name
 *   The machine name of the workflow.
 */
function workflow_deletewf($name) {
  $result = db_query("SELECT * FROM {workflow_states} WHERE workflow_name = '%s'", $name);
  while ($state = db_fetch_object($result)) {
    // Delete the state and any associated transitions and actions.
    workflow_state_delete($state->name);
    db_query("DELETE FROM {workflow_node} WHERE state_name = '%s'", $state->name);
  }
  db_query("DELETE FROM {workflow_type_map} WHERE workflow_name = '%s'", $name);
  db_query("DELETE FROM {workflows} WHERE name = '%s'", $name);
  // Notify any interested modules.
  module_invoke_all('workflow', 'workflow delete', $name, NULL, NULL);
  // Workflow deletion affects tabs (local tasks), so force menu rebuild.
  cache_clear_all('*', 'cache_menu', TRUE);
  menu_rebuild();
}

/**
 * Load all database and code workflow states. If $workflow_name is not passed,
 * all states for all workflows are given.
 *
 * @param $workflow_name
 *   The machine name of the workflow.
 * @return
 *   An array of workflow states.
 */
function workflow_get_states($workflow_name = NULL) {
  $workflows = workflow_load_all();

  if (is_null($workflow_name)) {
    static $all_states;

    if (is_null($all_states)) {
      $all_states = array();
      foreach ($workflows as $workflow) {
        $states = $workflow->states;
        foreach ($states as $name => $state) {
          $state->label = $workflow->label .': '. $state->label;
          $all_states[$name] = $state;
        }
      }
    }

    return $all_states;
  }

  return $workflows[$workflow_name]->states;
}

/**
 * Load workflow states for a workflow from the database. If $workflow_name is
 * not passed, all states for all workflows are given. States that have been
 * deleted are not included unless $all is TRUE.
 *
 * @param $workflow_name
 *   The machine name of the workflow.
 * @param $all
 *   If TRUE, will return all workflows, regardless of status.
 * @return
 *   An array of workflow states.
 */
function workflow_get_db_states($workflow_name = NULL, $all = FALSE) {
  $states = $query_args = array();
  $schema = drupal_get_schema('workflow_states');

  // Build up an array of WHERE clause arguments.
  $args = $all ? array() : array('status' => 1);
  if (isset($workflow_name)) {
    $args['workflow_name'] = $workflow_name;
  }

  // Construct the query.
  $query = "SELECT * FROM {workflow_states}";
  if (!empty($args)) {
    $conditions = array();
    foreach ($args as $key => $value) {
      if (isset($schema['fields'][$key])) {
        $conditions[] = "$key = " . db_type_placeholder($schema['fields'][$key]['type']);
        $query_args[] = $value;
      }
    }
    // Create the WHERE clause.
    $query .= " WHERE " . implode(' AND ', $conditions);
  }
  $query .= " ORDER BY weight, label";

  $result = db_query($query, $query_args);
  while ($state = db_fetch_object($result)) {
    $state->label = check_plain(t($state->label));

    // If we don't have a workflow_name, load up the associated workflow and
    // prepend the Workflow Label to the State Label.
    if (empty($workflow_name)) {
      $workflow = workflow_load($state->workflow_name);
      $state->label = $workflow->label .': '. $state->label;
    }

    $state->transitions = workflow_transitions_load($state->name);

    $states[$state->name] = $state;
  }

  return $states;
}

/**
 * Load workflow state labels for a workflow. If $workflow_name is not passed,
 * all states for all workflows are given. States that have been deleted are not
 * included.
 *
 * @param $workflow_name
 *   The machine name of the workflow.
 * @return
 *   An array of workflow states.
 */
function workflow_get_state_labels($workflow_name = NULL) {
  $states = workflow_get_states($workflow_name);

  foreach ($states as $name => $state) {
    $states[$name] = $state->label;
  }

  return $states;
}

/**
 * Given the machine name of a workflow state, return a keyed array representing
 * the state.
 *
 * @param $name
 *   The machine name of the workflow state.
 * @param $active_only
 *   Boolean to indicate to only return active states. Defaults to FALSE.
 * @return
 *   The workflow state object.
 */
function workflow_get_state($name, $active_only = FALSE) {
  $sql = "SELECT * FROM {workflow_states} WHERE name = '%s'";

  if ($active_only) {
    $sql .= " AND status = 1";
  }
  
  $result = db_query($sql, $name);
  // State IDs are unique, so there should be only one row.
  if ($state = db_fetch_object($result)) {
    $state->label = check_plain(t($state->label));
  }

  return $state;
}

/**
 * Given the ID of a state, return its name.
 *
 * @param integer $sid
 *   The ID of the workflow state.
 * @return string
 *   The name of the workflow state.
 */
function workflow_get_state_label($sid) {
  return db_result(db_query('SELECT label FROM {workflow_states} WHERE sid = %d', $sid));
}


/**
 * Add or update a workflow state to the database.
 *
 * @param $edit
 *   An array containing values for the new or updated workflow state.
 * @return
 *   The ID of the new or updated workflow state.
 */
function workflow_state_save($state) {
  if (!isset($state->sid)) {
    drupal_write_record('workflow_states', $state);
  }
  else {
    drupal_write_record('workflow_states', $state, 'sid');
  }

  if (isset($state->transitions)) {
    workflow_transitions_write($state->name, $state->transitions);
  }

  workflow_invalidate_cache();

  return $state;
}

/**
 * Delete a workflow state from the database, including any
 * transitions the state was involved in and any associations
 * with actions that were made to that transition.
 *
 * @param $name
 *   The machine name of the state to delete.
 * @param $new_name
 *   Deleting a state will leave any nodes to which that state is assigned
 *   without a state. If $new_state is given, it will be assigned to those
 *   orphaned nodes.
 */
function workflow_state_delete($name, $new_name = NULL) {
  if ($new_name && $new_state = workflow_get_state($new_name)) {
    // Assign nodes to new state so they are not orphaned.
    // A candidate for the batch API.
    $node = new stdClass();
    $node->workflow_stamp = time();
    $result = db_query("SELECT nid FROM {workflow_node} WHERE state_name = '%s'", $name);
    while ($data = db_fetch_object($result)) {
      $node->nid = $data->nid;
      $node->_workflow = $name;
      _workflow_write_history($node, $new_name, t('Previous state deleted'));
      db_query("UPDATE {workflow_node} SET state_name = '%s' WHERE nid = %d AND state_name = '%s'", $new_name, $data->nid, $name);
    }
  }
  else {
    // Go ahead and orphan nodes.
    db_query("DELETE from {workflow_node} WHERE state_name = '%s'", $name);
  }

  // Delete all associated transitions this state is involved in.
  $result = db_query("SELECT tid FROM {workflow_transitions} WHERE state_name = '%s' OR target_state_name = '%s'", $name, $name);
  while ($data = db_fetch_object($result)) {
    workflow_transition_delete($data->tid);
  }

  // Disable the state.
  db_query("UPDATE {workflow_states} SET status = 0 WHERE name = '%s'", $name);
  // Notify interested modules.
  module_invoke_all('workflow', 'state delete', $name, NULL, NULL);
}

/**
 * Load all transition records for a particular state.
 *
 * @param $from_state
 *   The state machine name that is being transitioned FROM.
 */
function workflow_transitions_load($from_state) {
  $transitions = array();
  $result = db_query("SELECT target_state_name, roles FROM {workflow_transitions} WHERE state_name = '%s'", $from_state);

  while ($transition = db_fetch_array($result)) {
    // Pull out the target_state_name and roles values into vars.
    extract($transition);
    $transitions[$target_state_name] = explode(',', $roles);
  }

  return $transitions;
}

/**
 * Delete a transition (and any associated actions).
 *
 * @param $tid
 *   The ID of the transition.
 */
function workflow_transition_delete($tid) {
  $actions = workflow_get_actions($tid);
  foreach ($actions as $aid => $type) {
    workflow_actions_remove($tid, $aid);
  }
  db_query("DELETE FROM {workflow_transitions} WHERE tid = %d", $tid);
  // Notify interested modules.
  module_invoke_all('workflow', 'transition delete', $tid, NULL, NULL);
}

/**
 * Validate target state and either execute a transition immediately or schedule
 * a transition to be executed later by cron.
 *
 * @param $object
 *   The entity in which the state is changing.
 * @param $state_name
 *   The machine name of the target state.
 * @param $workflow
 *   The workflow object for this entity.
 */
function workflow_transition($object, $state_name, $workflow) {
  // Make sure new state is a valid choice.
  if (array_key_exists($state_name, workflow_field_choices($object, $workflow))) {
    if (!$object->workflow_scheduled) {
      // It's an immediate change. Do the transition.
      $comment = isset($object->workflow_comment) ? $object->workflow_comment : NULL;
      workflow_execute_transition($object, $state_name, $workflow, $comment);
    }
    else {
      // Schedule the the time to change the state.
      $comment = $object->workflow_comment;
      $old_state_name = workflow_node_current_state($object);

      if ($object->workflow_scheduled_date['day'] < 10) {
        $object->workflow_scheduled_date['day'] = '0' .
        $object->workflow_scheduled_date['day'];
      }

      if ($object->workflow_scheduled_date['month'] < 10) {
        $object->workflow_scheduled_date['month'] = '0' .
        $object->workflow_scheduled_date['month'];
      }

      if (!$object->workflow_scheduled_hour) {
        $object->workflow_scheduled_hour = '00:00';
      }

      $scheduled = $object->workflow_scheduled_date['year'] . $object->workflow_scheduled_date['month'] . $object->workflow_scheduled_date['day'] . ' ' . $object->workflow_scheduled_hour . 'Z';
      if ($scheduled = strtotime($scheduled)) {
        // Adjust for user and site timezone settings.
        global $user;
        if (variable_get('configurable_timezones', 1) && $user->uid && strlen($user->timezone)) {
          $timezone = $user->timezone;
        }
        else {
          $timezone = variable_get('date_default_timezone', 0);
        }
        $scheduled = $scheduled - $timezone;

        // Clear previous entries and insert.
        db_query("DELETE FROM {workflow_scheduled_transition} WHERE nid = %d", $object->nid);
        db_query("INSERT INTO {workflow_scheduled_transition} (nid, old_state_name, state_name, scheduled, comment) VALUES (%d, '%s', '%s', %d, '%s')", $object->nid, $old_state_name, $state_name, $scheduled, $comment);

        $t_args = array(
          '@node_title' => $object->title,
          '%state_name' => workflow_get_state_label($state_name),
          '!scheduled_date' => format_date($scheduled),
        );
        watchdog('workflow', '@node_title scheduled for state change to %state_name on !scheduled_date', $t_args, WATCHDOG_NOTICE, l('view', "node/$object->nid/workflow"));
        drupal_set_message(t('@node_title is scheduled for state change to %state_name on !scheduled_date', $t_args));
      }
    }
  }
}

/**
 * Get allowable transitions based on roles. Typical use:
 *
 * @param $current_state
 *   The machine name of the current state.
 * @param $workflow
 *   The workflow object.
 * @param $account
 *   The user account to discover transitions for.
 *
 * @return
 *   Associative array of states ($state_name => $state_label pairs), excluding
 *   current state.
 */
function workflow_allowable_transitions($current_state, $workflow, $account = NULL) {
  if (empty($account)) {
    global $user;
    $account = $user;
  }

  $transitions = array();

  $available_transitions = $workflow->states[$current_state]->transitions;

  foreach ($workflow->states as $state_name => $state) {
    $allowed_roles = $available_transitions[$state_name];
    if ($account->uid == 1  // Superuser.
      || $state_name == $current_state // Include current state for same-state transitions.
      || workflow_transition_allowed($workflow, $current_state, $state_name, $account)) {
      $transitions[$state_name] = $state->label;
    }
  }

  return $transitions;
}



/**
 * Execute a transition (change state of a node).
 *
 * @param $node
 * @param $state_name
 *   The machine name of the target state.
 * @param $workflow
 *   The workflow object for this entity.
 * @param $comment
 *   A comment for the node's workflow history.
 * @param $force
 *   If set to TRUE, workflow permissions will be ignored.
 *
 * @return int
 *   ID of new state.
 */
function workflow_execute_transition($node, $state_name, $workflow, $comment = NULL, $force = FALSE) {
  global $user;
  $old_state = workflow_node_current_state($node);
  if ($old_state == $state_name) {
    // Stop if not going to a different state.
    // Write comment into history though.
    if ($comment && !$node->_workflow_scheduled_comment) {
      $node->workflow_stamp = time();
      db_query("UPDATE {workflow_node} SET stamp = %d WHERE nid = %d", $node->workflow_stamp, $node->nid);
      $result = module_invoke_all('workflow', 'transition pre', $old_state, $state_name, $node);
      _workflow_write_history($node, $state_name, $comment);
      unset($node->workflow_comment);

      $result = module_invoke_all('workflow', 'transition post', $old_state, $state_name, $node);
      // Rules integration
      if (module_exists('rules')) {
        rules_invoke_event('workflow_comment_added', $node, $old_state, $state_name);
      }
    }
    return;
  }

  $tid = workflow_get_transition_id($old_state, $state_name);
  if (!$tid && !$force) {
      watchdog('workflow', 'Attempt to go to nonexistent transition (from %old to %new)', array('%old' => $old_state, '%new' => $state_name, WATCHDOG_ERROR));
      return;
  }
  // Make sure this transition is valid and allowed for the current user.
  // Check allowability of state change if user is not superuser (might be cron).
  if (($user->uid != 1) && !$force) {
    $account = $user;
    if ($node->uid == $user->uid) {
      $account->roles['author'] = 'author';
    }
    if (!workflow_transition_allowed($workflow, $old_state, $state_name, $account)) {
      watchdog('workflow', 'User %user not allowed to go from state %old to %new', array('%user' => $user->name, '%old' => $old_state, '%new' => $state_name, WATCHDOG_NOTICE));
      return;
    }
  }
  // Invoke a callback indicating a transition is about to occur. Modules
  // may veto the transition by returning FALSE.
  $result = module_invoke_all('workflow', 'transition pre', $old_state, $state_name, $node);

  // Stop if a module says so.
  if (in_array(FALSE, $result)) {
    watchdog('workflow', 'Transition vetoed by module.');
    return;
  }

  // If the node does not have an existing $node->_workflow property, save
  // the $old_state there so _workflow_write_history() can log it.
  if (!isset($node->_workflow)) {
    $node->_workflow = $old_state;
  }
  // Change the state.
  _workflow_node_to_state($node, $state_name, $comment);
  $node->_workflow = $state_name;

  // Register state change with watchdog.
  $state_label = workflow_get_state_label($state_name);
  $type = node_get_types('name', $node->type);
  watchdog('workflow', 'State of @type %node_title set to %state_name', array('@type' => $type, '%node_title' => $node->title, '%state_name' => $state_label), WATCHDOG_NOTICE, l('view', 'node/' . $node->nid));

  // Notify modules that transition has occurred. Actions should take place
  // in response to this callback, not the previous one.
  module_invoke_all('workflow', 'transition post', $old_state, $state_name, $node);

  // Clear any references in the scheduled listing.
  db_query('DELETE FROM {workflow_scheduled_transition} WHERE nid = %d', $node->nid);

  // Rules integration
  if (module_exists('rules')) {
    rules_invoke_event('workflow_state_changed', $node, $old_state, $state_name);
  }

  return $state_name;
}

/**
 * Determine if a transition is allowed.
 *
 * @param $workflow
 *   The workflow object.
 * @param $from_state
 *   The machine name of the state being transitioned from.
 * @param $to_state
 *   The machine name of the state being transitioned to.
 * @return
 *   Boolean TRUE or FALSE.
 */
function workflow_transition_allowed($workflow, $from_state, $to_state, $account = NULL) {
  if (empty($account)) {
    global $user;
    $account = $user;
  }

  $roles = $workflow->states[$from_state]->transitions[$to_state];
  $allowed_roles = array_keys($account->roles);

  return (bool) array_intersect($roles, $allowed_roles);
}

/**
 * Get the tid of a transition, if it exists.
 *
 * @param $from
 *   The machine name of the originating state.
 * @param $to
 *   The machine name of the target state.
 * @return int
 *   Tid or FALSE if no such transition exists.
 */
function workflow_get_transition_id($from, $to) {
  return db_result(db_query("SELECT tid FROM {workflow_transitions} WHERE state_name = '%s' AND target_state_name = '%s'", $from, $to));
}

/**
 * Remove an action assignment programmatically.
 *
 * Helpful when deleting a workflow.
 *
 * @see workflow_transition_delete()
 *
 * @param $tid
 *   Transition ID.
 * @param $aid
 *   Action ID.
 */
function workflow_actions_remove($tid, $aid) {
  $ops = array();
  $result = db_query("SELECT op FROM {trigger_assignments} WHERE hook = 'workflow' AND aid = '%s'", $aid);
  while ($data = db_fetch_object($result)) {
    // Transition ID is the last part, e.g., foo-bar-1.
    $transition = array_pop(explode('-', $data->op));
    if ($tid == $transition) {
      $ops[] = $data->op;
    }
  }

  foreach ($ops as $op) {
    db_query("DELETE FROM {trigger_assignments} WHERE hook = 'workflow' AND op = '%s' AND aid = '%s'", $op, $aid);
    $description = db_result(db_query("SELECT description FROM {actions} WHERE aid = '%s'", $aid));
    watchdog('workflow', 'Action %action has been unassigned.',  array('%action' => $description));
  }
}

/**
 * Get the states current user can move to.
 *
 * @param $object
 *   The entity to determine choices for.
 * @param $workflow
 *   The workflow to determine the options on.
 * @return
 *   Array of transitions.
 */
function workflow_field_choices($object, $workflow) {
  global $user;
  $account = $user;

  $current_state = workflow_node_current_state($object);

  // If user is node author or this is a new page, give the authorship role.
  if (($user->uid == $object->uid && $object->uid > 0) || (arg(0) == 'node' && arg(1) == 'add')) {
    $account->roles['author'] = 'author';
  }
  $transitions = workflow_allowable_transitions($current_state, $workflow, $account);

  return $transitions;
}

/**
 * Get the current state of a given node.
 *
 * @param $node
 *   The node to check.
 * @return
 *   The machine name of the current state.
 */
function workflow_node_current_state($node) {
  $state_name = FALSE;

  // There is no nid when creating a node.
  if (!empty($node->nid)) {
    $state_name = db_result(db_query('SELECT state_name FROM {workflow_node} WHERE nid = %d', $node->nid));
  }

  if (!$state_name && !empty($node->type)) {
    // No current state. Use creation state.
    $workflow = workflow_get_workflow_for_type($node->type);
    $state_name = _workflow_creation_state($workflow->name);
  }
  return $state_name;
}

/**
 * Put a node into a state.
 * No permission checking here; only call this from other functions that know
 * what they're doing.
 *
 * @see workflow_execute_transition()
 *
 * @param object $node
 * @param $state_name
 *   The machine name of the target state.
 */
function _workflow_node_to_state($node, $state_name, $comment = NULL) {
  global $user;
  $node->workflow_stamp = time();
  if (db_result(db_query("SELECT nid FROM {workflow_node} WHERE nid = %d", $node->nid))) {
    db_query("UPDATE {workflow_node} SET state_name = '%s', uid = %d, stamp = %d WHERE nid = %d", $state_name, $user->uid, $node->workflow_stamp, $node->nid);
  }
  else {
    db_query("INSERT INTO {workflow_node} (nid, state_name, uid, stamp) VALUES (%d, '%s', %d, %d)", $node->nid, $state_name, $user->uid, $node->workflow_stamp);
  }
  _workflow_write_history($node, $state_name, $comment);
}

/**
 * Write the node history record to workflow_node_history.
 */
function _workflow_write_history($node, $state_name, $comment) {
  global $user;
  db_query("INSERT INTO {workflow_node_history} (nid, old_state_name, state_name, uid, comment, stamp) VALUES (%d, '%s', '%s', %d, '%s', %d)", $node->nid, $node->_workflow, $state_name, $user->uid, $comment, $node->workflow_stamp);
}

/**
 * Get a list of roles.
 *
 * @return
 *   Array of role names keyed by role ID, including the 'author' role.
 */
function workflow_get_roles() {
  static $roles = NULL;
  if (!$roles) {
    $result = db_query('SELECT * FROM {role} ORDER BY name');
    $roles = array('author' => 'author');
    while ($data = db_fetch_object($result)) {
      $roles[$data->rid] = check_plain($data->name);
    }
  }
  return $roles;
}

/**
 * Helper function to generate a creation state name.
 *
 * @param $name
 *   The machine name of the workflow.
 */
function _workflow_creation_state($name) {
  return $name .'_'. WORKFLOW_CREATION_NAME;
}

