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
      $workflow->node_types = workflow_get_types($workflow->name);
    }
  }
}

/**
 * Get all the node types for a workflow.
 *
 * @param $name
 *   The machine name of the workflow
 * @return
 *   An array of node types for this workflow.
 */
function workflow_get_types($name) {
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
 * Get ID of a workflow for a node type.
 *
 * @param $type
 *   Machine readable node type name, e.g. 'story'.
 * @return string
 *   The machine name of the workflow or FALSE if no workflow is mapped to this type.
 */
function workflow_get_workflow_for_type($type) {
  static $cache;

  if(empty($cache[$type])) {
    $sql = "SELECT workflow_name FROM {workflow_type_map} WHERE type = '%s'";
    $cache[$type] = db_result(db_query($sql, $type));
  }

  return empty($cache[$type]) ? FALSE : $cache[$type];
}

/**
 * Save mapping of workflow to node type. E.g., "the story node type is using
 * the Foo workflow."
 *
 * @param $form_state['values']
 */
function workflow_types_save($form_values) {
  db_query("DELETE FROM {workflow_type_map}");
  $node_types = node_get_types();
  foreach ($node_types as $type => $name) {
    db_query("INSERT INTO {workflow_type_map} (type, workflow_name) VALUES ('%s', '%s')", $type, $form_values[$type]['workflow_name']);
    variable_set('workflow_' . $type, array_keys(array_filter(($form_values[$type]['placement']))));
  }
}