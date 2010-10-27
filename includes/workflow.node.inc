<?php
// $Id$

/**
 * @file
 * Node module implementations for Workflow.
 * @todo, move this into a separate module.
 */

/**
 * Implementation of hook_workflows_alter() on behalf of node module.
 */
function node_workflows_alter(&$workflows) {
  foreach ($workflows as &$workflow) {
    if (!isset($workflow->node_types)) {
      $workflow->node_types = workflow_get_db_node_types($workflow->name);
    }
  }
}

/**
 * Load up an array map of node types to workflows and workflow to node types.
 *
 * @param $orientation
 *   String, either 'node_type' or 'workflow'.
 * @return
 *   An array map of node types to workflows, or workflow to node types, based
 *   on $orientation. If $orientation is node_type, the array keys will be
 *   node types, and the values will be the workflow name. If $orientation is
 *   workflow, the keys will be the workflow's machine name and the value will
 *   be an array of node types for that workflow.
 */
function workflow_node_type_map_load($orientation = 'node_type') {
  static $map;
  
  $workflows = workflow_load_all();

  if (empty($map)) {
    foreach (workflow_load_all() as $workflow) {
      $node_types = isset($workflow->node_types) ? $workflow->node_types : array();
      $map['workflow'][$workflow->name] = $node_types;
      foreach ($node_types as $type) {
        $map['node_type'][$type] = $workflow->name;
      }
    }
  }

  return $map[$orientation];
}

/**
 * Get all the node types for a workflow.
 *
 * @param $name
 *   The machine name of the workflow
 * @return
 *   An array of node types for this workflow.
 */
function workflow_get_db_node_types($name) {
  static $type_map;

  if (empty($type_map)) {
    $type_map = array();
    $result = db_query("SELECT * FROM {workflow_type_map}");
    while ($record = db_fetch_object($result)) {
      $type_map[$record->workflow_name][] = $record->type;
    }
  }

  return $type_map[$name];
}

/**
 * Get a workflow for a node type.
 *
 * @param $type
 *   Machine readable node type name, e.g. 'story'.
 * @return
 *   The workflow object.
 */
function workflow_get_workflow_for_type($type) {
  static $cache;

  if (!isset($cache)) {
    $cache = array();
    foreach (workflow_load_all() as $workflow) {
      if (!empty($workflow->node_types)) {
        foreach ($workflow->node_types as $node_type) {
          $cache[$node_type] = $workflow;
        }
      }
    }
  }

  return empty($cache[$type]) ? FALSE : $cache[$type];
}

/**
 * Save mapping of workflow to node type. E.g., "the story node type is using
 * the Foo workflow."
 *
 * @param $form_state['values']
 */
function workflow_node_type_save($workflow_name, $node_type) {
  db_query("DELETE FROM {workflow_type_map} WHERE node_type = '%s'", $node_type);
  $type_map = new stdClass();
  $type_map->node_type = $node_type;
  $type_map->workflow_name = $workflow_name;
  return drupal_write_record('workflow_type_map', $type_map);
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
